#include "ble_hid.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "microreader/HidReport.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"

extern "C" void ble_store_config_init(void);

namespace ble_hid {
namespace {

using microreader::BluetoothDevice;
using microreader::IBluetooth;
using State = IBluetooth::State;

const char* TAG = "ble_hid";
constexpr const char* kNvsNamespace = "microreader";
constexpr const char* kNvsEnabled = "ble_on";
constexpr const char* kNvsName = "ble_name";

constexpr uint16_t kUuidHidService = 0x1812;
constexpr uint16_t kUuidReport = 0x2A4D;
constexpr uint16_t kUuidBootKeyboardInput = 0x2A22;
constexpr uint16_t kUuidCccd = 0x2902;

// Scan timing in 0.625 ms units, connection timing in 1.25 ms units.
constexpr uint16_t kFastScanItvl = 320;   // 200 ms
constexpr uint16_t kFastScanWindow = 32;  // 20 ms  → 10% duty
constexpr uint16_t kSlowScanItvl = 1024;  // 640 ms
constexpr uint16_t kSlowScanWindow = 48;  // 30 ms  → ~5% duty
constexpr int32_t kFastReconnectMs = 30000;
constexpr int32_t kPairScanMs = 60000;
constexpr int32_t kPairConnectMs = 10000;

constexpr int kMaxFound = 8;
constexpr int kMaxReports = 8;
constexpr int kQueueSize = 16;

struct Found {
  BluetoothDevice dev;
  ble_addr_t addr;
};

struct Report {
  uint16_t val_handle;
  uint16_t end_handle;  // last handle of this characteristic's descriptors
  uint16_t cccd_handle;
  uint16_t uuid;
  uint8_t held;  // hid::Action bits held at the last notification
};

class Esp32Bluetooth final : public IBluetooth {
 public:
  // --- IBluetooth (UI task) ---
  bool enabled() const override {
    return enabled_;
  }
  State state() const override {
    return state_.load();
  }
  const char* remote_name() const override {
    return name_;
  }
  bool has_remote() const override {
    return running_ ? has_bond_ : name_[0] != '\0';
  }
  uint32_t revision() const override {
    return revision_.load();
  }

  void set_enabled(bool on) override {
    if (on == enabled_)
      return;
    enabled_ = on;
    save_enabled_();
    if (on)
      start_stack_();
    else
      stop_stack_();
    bump_();
  }

  void forget_remote() override {
    if (!running_)
      return;
    has_bond_ = false;
    if (ble_gap_conn_active())
      ble_gap_conn_cancel();
    if (conn_ != BLE_HS_CONN_HANDLE_NONE)
      ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
    set_name_("");
    set_state_(State::NotPaired);
  }

  void start_pairing() override {
    if (!running_ || !synced_)
      return;
    set_state_(State::Pairing);  // before cancel/terminate: suppresses auto-reconnect
    if (ble_gap_conn_active())
      ble_gap_conn_cancel();
    if (conn_ != BLE_HS_CONN_HANDLE_NONE)
      ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM);
    portENTER_CRITICAL(&lock_);
    found_count_ = 0;
    portEXIT_CRITICAL(&lock_);

    ble_gap_disc_params p{};
    p.itvl = 96;    // 60 ms
    p.window = 48;  // 30 ms
    p.passive = 0;  // active: names usually arrive in the scan response
    p.filter_duplicates = 0;
    const int rc = ble_gap_disc(own_addr_type_, kPairScanMs, &p, gap_event_, nullptr);
    if (rc != 0) {
      ESP_LOGW(TAG, "pairing scan failed: %d", rc);
      resume_();
    }
  }

  void stop_pairing() override {
    if (state_.load() != State::Pairing)
      return;
    ble_gap_disc_cancel();
    resume_();
  }

  int pairing_devices(BluetoothDevice* out, int max) const override {
    portENTER_CRITICAL(&lock_);
    const int n = found_count_ < max ? found_count_ : max;
    for (int i = 0; i < n; ++i)
      out[i] = found_[i].dev;
    portEXIT_CRITICAL(&lock_);
    return n;
  }

  void pair(int index) override {
    if (state_.load() != State::Pairing)
      return;
    portENTER_CRITICAL(&lock_);
    const bool valid = index >= 0 && index < found_count_;
    Found target{};
    if (valid)
      target = found_[index];
    portEXIT_CRITICAL(&lock_);
    if (!valid)
      return;

    ble_gap_disc_cancel();
    std::snprintf(pending_name_, sizeof(pending_name_), "%s", target.dev.name);
    pairing_connect_ = true;
    set_state_(State::Connecting);
    const int rc = connect_(target.addr, kPairConnectMs, 48, 48);  // 100% duty, short
    if (rc != 0) {
      ESP_LOGW(TAG, "pair connect failed: %d", rc);
      pairing_connect_ = false;
      resume_();
    }
  }

  // --- boot / shutdown (main task) ---
  void boot() {
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &nvs) == ESP_OK) {
      uint8_t on = 0;
      nvs_get_u8(nvs, kNvsEnabled, &on);
      size_t len = sizeof(name_);
      if (nvs_get_str(nvs, kNvsName, name_, &len) != ESP_OK)
        name_[0] = '\0';
      nvs_close(nvs);
      enabled_ = on != 0;
    }
    if (enabled_)
      start_stack_();
  }

  void shutdown() {
    stop_stack_();
  }

  int take_presses(uint8_t* out, int max) {
    int n = 0;
    portENTER_CRITICAL(&lock_);
    while (q_head_ != q_tail_ && n < max) {
      out[n++] = queue_[q_head_];
      q_head_ = (q_head_ + 1) % kQueueSize;
    }
    portEXIT_CRITICAL(&lock_);
    return n;
  }

 private:
  // ---------------------------------------------------------------- stack
  void start_stack_() {
    if (running_)
      return;
    const esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "nimble_port_init failed: %d", err);
      enabled_ = false;
      return;
    }
    ble_hs_cfg.sync_cb = on_sync_;
    ble_hs_cfg.reset_cb = on_reset_;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;  // Just Works
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    stopping_ = false;
    running_ = true;
    set_state_(State::NotPaired);
    nimble_port_freertos_init(host_task_);
  }

  void stop_stack_() {
    if (!running_)
      return;
    stopping_ = true;
    if (conn_ != BLE_HS_CONN_HANDLE_NONE) {
      // Let the remote know right away so it can go back to sleep, instead
      // of it waiting out the supervision timeout.
      ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM);
      vTaskDelay(pdMS_TO_TICKS(60));
    }
    nimble_port_stop();
    nimble_port_deinit();
    running_ = false;
    synced_ = false;
    conn_ = BLE_HS_CONN_HANDLE_NONE;
    report_count_ = 0;
    set_state_(State::Off);
  }

  static void host_task_(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
  }

  static void on_reset_(int reason) {
    ESP_LOGW(TAG, "host reset: %d", reason);
    self().synced_ = false;
  }

  static void on_sync_() {
    Esp32Bluetooth& s = self();
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s.own_addr_type_);
    s.synced_ = true;

    ble_addr_t peers[1];
    int n = 0;
    ble_store_util_bonded_peers(peers, &n, 1);
    s.has_bond_ = n > 0;
    if (s.has_bond_)
      s.peer_ = peers[0];
    s.resume_();
  }

  // Back to the resting state: look for the bonded remote, or idle.
  void resume_() {
    if (has_bond_) {
      set_state_(State::Searching);
      reconnect_(true);
    } else {
      set_state_(State::NotPaired);
    }
  }

  void reconnect_(bool fast) {
    if (stopping_ || !has_bond_)
      return;
    const int rc = fast ? connect_(peer_, kFastReconnectMs, kFastScanItvl, kFastScanWindow)
                        : connect_(peer_, BLE_HS_FOREVER, kSlowScanItvl, kSlowScanWindow);
    if (rc != 0 && rc != BLE_HS_EALREADY)
      ESP_LOGW(TAG, "reconnect failed: %d", rc);
  }

  int connect_(const ble_addr_t& addr, int32_t duration_ms, uint16_t scan_itvl, uint16_t scan_window) {
    ble_gap_conn_params p{};
    p.scan_itvl = scan_itvl;
    p.scan_window = scan_window;
    p.itvl_min = 24;  // 30 ms: quick service discovery; relaxed once ready
    p.itvl_max = 40;  // 50 ms
    p.latency = 0;
    p.supervision_timeout = 400;  // 4 s
    return ble_gap_connect(own_addr_type_, &addr, duration_ms, &p, gap_event_, nullptr);
  }

  // ------------------------------------------------------------ GAP events
  static int gap_event_(ble_gap_event* ev, void*) {
    Esp32Bluetooth& s = self();
    switch (ev->type) {
      case BLE_GAP_EVENT_DISC:
        s.on_advert_(ev->disc);
        return 0;

      case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s.state_.load() == State::Pairing)
          s.resume_();
        return 0;

      case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
          s.conn_ = ev->connect.conn_handle;
          s.report_count_ = 0;
          s.set_state_(State::Connecting);
          ble_gap_security_initiate(s.conn_);
        } else if (!s.stopping_ && s.state_.load() != State::Pairing) {
          if (s.pairing_connect_) {
            s.pairing_connect_ = false;
            s.resume_();
          } else {
            s.reconnect_(false);  // fast window elapsed: settle into slow duty
          }
        }
        return 0;

      case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected: 0x%x", ev->disconnect.reason);
        s.conn_ = BLE_HS_CONN_HANDLE_NONE;
        s.report_count_ = 0;
        s.pairing_connect_ = false;
        if (!s.stopping_ && s.state_.load() != State::Pairing)
          s.resume_();
        return 0;

      case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status == 0) {
          s.on_encrypted_();
        } else {
          ESP_LOGW(TAG, "encryption failed: %d", ev->enc_change.status);
          ble_gap_terminate(ev->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

      case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The remote lost its keys (reset, re-paired elsewhere): drop ours
        // and pair again rather than refusing it.
        ble_gap_conn_desc desc;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &desc) == 0)
          ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
      }

      case BLE_GAP_EVENT_NOTIFY_RX:
        s.on_notify_(ev->notify_rx.attr_handle, ev->notify_rx.om);
        return 0;

      default:
        return 0;
    }
  }

  void on_advert_(const ble_gap_disc_desc& d) {
    ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d.data, d.length_data) != 0)
      return;

    portENTER_CRITICAL(&lock_);
    int idx = -1;
    for (int i = 0; i < found_count_; ++i)
      if (ble_addr_cmp(&found_[i].addr, &d.addr) == 0)
        idx = i;
    portEXIT_CRITICAL(&lock_);

    if (idx < 0) {
      bool hid = f.appearance_is_present && (f.appearance & 0xFFC0) == 0x03C0;
      for (int i = 0; i < f.num_uuids16 && !hid; ++i)
        hid = ble_uuid_u16(&f.uuids16[i].u) == kUuidHidService;
      if (!hid)
        return;
    }

    char name[sizeof(BluetoothDevice::name)] = {};
    if (f.name_len) {
      const size_t n = f.name_len < sizeof(name) - 1 ? f.name_len : sizeof(name) - 1;
      std::memcpy(name, f.name, n);
    }

    bool changed = false;
    portENTER_CRITICAL(&lock_);
    if (idx < 0 && found_count_ < kMaxFound) {
      idx = found_count_++;
      found_[idx] = Found{};
      found_[idx].addr = d.addr;
      std::snprintf(found_[idx].dev.name, sizeof(found_[idx].dev.name), "Remote %02X:%02X", d.addr.val[1],
                    d.addr.val[0]);
      changed = true;
    }
    if (idx >= 0) {
      found_[idx].dev.rssi = d.rssi;
      if (name[0] && std::strcmp(found_[idx].dev.name, name) != 0) {
        std::memcpy(found_[idx].dev.name, name, sizeof(name));
        changed = true;
      }
    }
    char shown[sizeof(BluetoothDevice::name)] = {};
    if (changed && idx >= 0)
      std::memcpy(shown, found_[idx].dev.name, sizeof(shown));
    portEXIT_CRITICAL(&lock_);
    if (changed) {
      ESP_LOGI(TAG, "found: %s (%d dBm)", shown, d.rssi);
      bump_();  // RSSI alone doesn't redraw the panel
    }
  }

  void on_encrypted_() {
    ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_, &desc) != 0)
      return;
    if (pairing_connect_) {
      // Single-remote policy: the new bond replaces any older one.
      pairing_connect_ = false;
      ble_addr_t peers[2];
      int n = 0;
      ble_store_util_bonded_peers(peers, &n, 2);
      for (int i = 0; i < n; ++i)
        if (ble_addr_cmp(&peers[i], &desc.peer_id_addr) != 0)
          ble_store_util_delete_peer(&peers[i]);
      peer_ = desc.peer_id_addr;
      has_bond_ = true;
      set_name_(pending_name_);
    }
    discover_();
  }

  // -------------------------------------------------- HID-over-GATT setup
  // Chain: HID service → its characteristics → descriptors → CCCD writes.
  void discover_() {
    svc_start_ = svc_end_ = 0;
    report_count_ = 0;
    subscribed_ = 0;
    static const ble_uuid16_t kHid = BLE_UUID16_INIT(kUuidHidService);
    ble_gattc_disc_svc_by_uuid(conn_, &kHid.u, on_svc_, nullptr);
  }

  static int on_svc_(uint16_t conn, const ble_gatt_error* err, const ble_gatt_svc* svc, void*) {
    Esp32Bluetooth& s = self();
    if (err->status == 0 && svc) {
      if (!s.svc_start_)
        s.svc_start_ = svc->start_handle;
      s.svc_end_ = svc->end_handle;
    } else if (err->status == BLE_HS_EDONE && s.svc_start_) {
      ble_gattc_disc_all_chrs(conn, s.svc_start_, s.svc_end_, on_chr_, nullptr);
    } else {
      s.fail_setup_("no HID service", err->status);
    }
    return 0;
  }

  static int on_chr_(uint16_t, const ble_gatt_error* err, const ble_gatt_chr* chr, void*) {
    Esp32Bluetooth& s = self();
    if (err->status == 0 && chr) {
      // A characteristic's descriptors end where the next characteristic
      // begins; the last one's run to the end of the service.
      if (s.report_count_ && s.reports_[s.report_count_ - 1].end_handle == 0)
        s.reports_[s.report_count_ - 1].end_handle = chr->def_handle - 1;
      if (chr->uuid.u.type != BLE_UUID_TYPE_16 || !(chr->properties & BLE_GATT_CHR_PROP_NOTIFY))
        return 0;
      const uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
      if ((uuid == kUuidReport || uuid == kUuidBootKeyboardInput) && s.report_count_ < kMaxReports)
        s.reports_[s.report_count_++] = Report{chr->val_handle, 0, 0, uuid, 0};
    } else if (err->status == BLE_HS_EDONE && s.report_count_) {
      if (s.reports_[s.report_count_ - 1].end_handle == 0)
        s.reports_[s.report_count_ - 1].end_handle = s.svc_end_;
      s.discover_dscs_from_(0);
    } else {
      s.fail_setup_("no input reports", err->status);
    }
    return 0;
  }

  // Descriptor discovery runs once per report: NimBLE reports every
  // descriptor it finds against the start handle it was given, so a single
  // service-wide pass can't tell which report a CCCD belongs to.
  void discover_dscs_from_(int i) {
    for (; i < report_count_; ++i) {
      const Report& r = reports_[i];
      if (r.end_handle <= r.val_handle)
        continue;
      ble_gattc_disc_all_dscs(conn_, r.val_handle, r.end_handle, on_dsc_,
                              reinterpret_cast<void*>(static_cast<intptr_t>(i)));
      return;
    }
    subscribe_from_(0);
  }

  static int on_dsc_(uint16_t, const ble_gatt_error* err, uint16_t, const ble_gatt_dsc* dsc, void* arg) {
    Esp32Bluetooth& s = self();
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    if (err->status == 0 && dsc) {
      if (dsc->uuid.u.type == BLE_UUID_TYPE_16 && ble_uuid_u16(&dsc->uuid.u) == kUuidCccd)
        s.reports_[i].cccd_handle = dsc->handle;
    } else if (err->status == BLE_HS_EDONE) {
      s.discover_dscs_from_(i + 1);
    } else {
      s.fail_setup_("descriptor discovery", err->status);
    }
    return 0;
  }

  void subscribe_from_(int i) {
    // Report protocol (the default) delivers keys on Report characteristics;
    // the boot-keyboard characteristic is only a fallback for devices that
    // offer nothing else, otherwise every key would arrive twice.
    bool has_report = false;
    for (int k = 0; k < report_count_; ++k)
      has_report |= reports_[k].uuid == kUuidReport;
    for (; i < report_count_; ++i) {
      const Report& r = reports_[i];
      if (!r.cccd_handle || (has_report && r.uuid != kUuidReport))
        continue;
      static const uint8_t kNotifyOn[2] = {1, 0};
      ble_gattc_write_flat(conn_, r.cccd_handle, kNotifyOn, sizeof(kNotifyOn), on_subscribed_,
                           reinterpret_cast<void*>(static_cast<intptr_t>(i)));
      return;
    }
    on_ready_();
  }

  static int on_subscribed_(uint16_t, const ble_gatt_error* err, ble_gatt_attr*, void* arg) {
    if (err->status == 0)
      ++self().subscribed_;
    else
      ESP_LOGW(TAG, "subscribe failed: %d", err->status);
    self().subscribe_from_(static_cast<int>(reinterpret_cast<intptr_t>(arg)) + 1);
    return 0;
  }

  void on_ready_() {
    ESP_LOGI(TAG, "remote ready: %d of %d reports subscribed", subscribed_, report_count_);
    if (subscribed_ == 0) {
      fail_setup_("nothing to subscribe", 0);
      return;
    }
    // Relax the link now that setup is done: the remote can still answer on
    // any event, so a key press arrives within one interval (≤120 ms).
    ble_gap_upd_params p{};
    p.itvl_min = 72;  // 90 ms
    p.itvl_max = 96;  // 120 ms
    p.latency = 4;
    p.supervision_timeout = 600;  // 6 s
    ble_gap_update_params(conn_, &p);
    set_state_(State::Connected);
  }

  void fail_setup_(const char* what, int status) {
    ESP_LOGW(TAG, "HID setup failed (%s): %d", what, status);
    ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM);
  }

  // ------------------------------------------------------------ key input
  void on_notify_(uint16_t handle, os_mbuf* om) {
    Report* r = nullptr;
    for (int i = 0; i < report_count_; ++i)
      if (reports_[i].val_handle == handle)
        r = &reports_[i];
    if (!r)
      return;

    uint8_t data[16];
    const uint16_t len = OS_MBUF_PKTLEN(om) < sizeof(data) ? OS_MBUF_PKTLEN(om) : sizeof(data);
    os_mbuf_copydata(om, 0, len, data);

    const uint8_t held = microreader::hid::report_actions(data, len);
    const uint8_t pressed = held & ~r->held;
    r->held = held;

    if (held == 0) {
      bool any = false;
      for (uint16_t i = 0; i < len; ++i)
        any |= data[i] != 0;
      if (any)  // a key we don't map — log it so new remotes can be added
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);
    }

    for (uint8_t bit = 1; bit; bit <<= 1)
      if (pressed & bit)
        enqueue_(static_cast<uint8_t>(microreader::hid::action_button(static_cast<microreader::hid::Action>(bit))));
  }

  void enqueue_(uint8_t button) {
    portENTER_CRITICAL(&lock_);
    const int next = (q_tail_ + 1) % kQueueSize;
    if (next != q_head_) {
      queue_[q_tail_] = button;
      q_tail_ = next;
    }
    portEXIT_CRITICAL(&lock_);
  }

  // --------------------------------------------------------------- misc
  void set_state_(State st) {
    if (state_.exchange(st) != st) {
      ESP_LOGI(TAG, "state -> %d", static_cast<int>(st));
      bump_();
    }
  }
  void bump_() {
    revision_.fetch_add(1);
  }

  void set_name_(const char* name) {
    std::snprintf(name_, sizeof(name_), "%s", name);
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &nvs) == ESP_OK) {
      if (name_[0])
        nvs_set_str(nvs, kNvsName, name_);
      else
        nvs_erase_key(nvs, kNvsName);
      nvs_commit(nvs);
      nvs_close(nvs);
    }
    bump_();
  }

  void save_enabled_() {
    nvs_handle_t nvs;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &nvs) == ESP_OK) {
      nvs_set_u8(nvs, kNvsEnabled, enabled_ ? 1 : 0);
      nvs_commit(nvs);
      nvs_close(nvs);
    }
  }

  static Esp32Bluetooth& self();

  bool enabled_ = false;
  bool running_ = false;
  volatile bool synced_ = false;
  volatile bool stopping_ = false;
  volatile bool has_bond_ = false;
  volatile bool pairing_connect_ = false;
  std::atomic<State> state_{State::Off};
  std::atomic<uint32_t> revision_{0};

  uint8_t own_addr_type_ = BLE_OWN_ADDR_PUBLIC;
  ble_addr_t peer_{};
  char name_[sizeof(BluetoothDevice::name)] = {};
  char pending_name_[sizeof(BluetoothDevice::name)] = {};

  volatile uint16_t conn_ = BLE_HS_CONN_HANDLE_NONE;
  uint16_t svc_start_ = 0;
  uint16_t svc_end_ = 0;
  Report reports_[kMaxReports] = {};
  int report_count_ = 0;
  int subscribed_ = 0;

  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Found found_[kMaxFound] = {};
  int found_count_ = 0;
  uint8_t queue_[kQueueSize] = {};
  int q_head_ = 0;
  int q_tail_ = 0;
};

Esp32Bluetooth g_bt;

Esp32Bluetooth& Esp32Bluetooth::self() {
  return g_bt;
}

}  // namespace

void boot() {
  g_bt.boot();
}

void shutdown() {
  g_bt.shutdown();
}

int take_presses(uint8_t* out, int max) {
  return g_bt.take_presses(out, max);
}

microreader::IBluetooth& instance() {
  return g_bt;
}

}  // namespace ble_hid
