# Local WebCore page diagnostic (unfinished)

This fixture is being compiled against the actual full CHOS WebCore target.
It has not linked or executed yet. No rendering or browser acceptance is claimed.

It follows the original internal headless `Page` workflow used by `SVGImage`,
loads only a trusted embedded HTML document, sizes a real frame view, and uses
the original parser, CSS layout, JavaScript DOM bindings and Cairo graphics.
The sandbox restriction is explicitly relaxed only for this fixture's own
document so JavaScript can execute. No network or production browser client is
supplied by the upstream empty clients.

The intended checks are actual DOM rectangles, complete flat-color CSS pixels,
font glyph coverage, and changed geometry, transform, color and text after a
JavaScript mutation. Fontconfig must receive the existing five font files at
`/FONTS/F0.TTF` through `/FONTS/F4.TTF` before the engine starts.
Owned-window presentation, input, lifecycle qualification and the full browser
remain future work until this basic page target builds and passes.
