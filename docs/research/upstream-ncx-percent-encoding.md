# Upstream: NCX percent-encoded paths

Question: Does the original `CidVonHighwind/microreader` firmware have the
same defect in which an NCX `content/@src` such as `CR%21chapter.xhtml` fails
to match the ZIP entry `CR!chapter.xhtml`?

## Finding

**Yes.** Upstream `main` at commit
[`a4adfdb`](https://github.com/CidVonHighwind/microreader/blob/a4adfdb9a37b83bcb724cac3dc8990f335b49e8a/lib/microreader/content/EpubParser.cpp#L364-L423)
has the same NCX lookup implementation as this repository for this path.
It copies `content/@src`, removes the fragment, applies path-segment
normalization, then compares that still-encoded string directly with the ZIP
entry name.  There is no percent-decoding operation between reading `src` and
the equality comparison.  Consequently, `%21` remains the three literal
characters `%`, `2`, `1`, and cannot equal a ZIP filename containing `!`.

This conclusion is an inference from that source code and the specific
observed EPUB pair (`CR%21…` in NCX versus `CR!…` in the ZIP); it does not rely
on a claim that the upstream project has reproduced this exact book.

## History checked

Upstream commit
[`95e603a` — “resolve .. in toc paths”](https://github.com/CidVonHighwind/microreader/commit/95e603acc1493def7b019fb28f6e136327b1d01a)
introduced `normalize_path()` and applied it to the NCX source path.  Its
diff handles `.` and `..` path segments only; it adds no URI percent decoder.
The later upstream `main` source linked above still calls only
`normalize_path(root_dir + src)` before the exact ZIP-name comparison.

## Scope

The result applies to the original upstream firmware at the recorded `main`
commit (the upstream remote currently tracked by this checkout).  A firmware
build made from that revision will therefore fail this book’s TOC resolution
in the same way, unless it contains a separate, unexamined local patch.

## EPUB/URI conformance and interoperability

This is **not evidence of a malformed or unusually exotic EPUB**. The NCX
`content/@src` value is a URI: the DAISY NCX specification defines it as the
URI pointing to the referenced document part
([DAISY Z39.86-2005, `content/@src`](https://www.daisy.org/z3986/2005/Z39-86-2005.pdf)).
EPUB 2.0.1 in turn explicitly treats resource-reference attributes as URI
values and normatively refers to RFC 2396
([OPS 2.0.1 §2.3.1](https://idpf.org/epub/20/spec/OPS_2.0.1_draft.htm#Section2.3.1)).

The affected file declares `version="2.0"`, so that older EPUB-2 URI model is
its relevant authoring context. Under RFC 2396, `!` is an *unreserved* `mark`
character, and an unreserved character may be written in escaped form without
changing the URI's semantics
([RFC 2396 §2.3](https://datatracker.ietf.org/doc/html/rfc2396#section-2.3)).
In that older model, `CR%21chapter.xhtml` and `CR!chapter.xhtml` therefore
identify the same resource. Percent-encoding `!` is a somewhat over-cautious
producer choice (the RFC says it generally should not be necessary), but is
not itself a sign of a corrupt EPUB; the RFC even notes that systems
automatically escape some such `mark` characters.

The literal `!` in the ZIP/OCF filename is also ordinary. Later OCF rules make
the general model explicit: files reference each other using relative IRI
references, filenames are UTF-8, and `!` is absent from the prohibited-name
character list ([EPUB OCF 3.2 §§3.3–3.4](https://www.w3.org/publishing/epub32/epub-ocf.html#sec-file-and-directory-structure)).
The current EPUB specification further illustrates that a content URL may
serialize a filename character as percent encoding (for example, a space as
`%20`) ([EPUB 3.3 §4.2.5](https://www.w3.org/TR/epub-33/#sec-container-urls)).

There is a standards-version caveat: RFC 3986 later reclassified `!` as a
reserved sub-delimiter, and current EPUB resolves URL strings using the URL
parser. It does not make `%21` and literal `!` interchangeable by generic URL
comparison ([RFC 3986 §§2.2, 2.4](https://www.rfc-editor.org/rfc/rfc3986#section-2.2);
[EPUB 3.3 §4.2.5](https://www.w3.org/TR/epub-33/#sec-container-urls)). Thus
this pattern should not be produced for new EPUBs, but is a plausible legacy
EPUB-2 interoperability case.

**Practical conclusion:** support the book, but as a deliberately forgiving
fallback — not as a blind global URI transformation. First perform the normal
exact ZIP-name lookup. Only if it fails, decode the NCX path exactly once and
accept it only if it names an existing entry; reject decoded `/` or `\\` inside
a path segment and preserve the existing dot-segment containment checks. Add a
regression fixture with literal `!` in the ZIP entry and `%21` in `toc.ncx`.
Never decode twice: otherwise `%25` becomes ambiguous (see
[RFC 3986 §2.4](https://www.rfc-editor.org/rfc/rfc3986#section-2.4)).
