# Basic setup 

A victim/attacker setup. 
It selects a pixel, verify origin isolation, and record baseline frame timing. Does not yet apply the paper's subnormal SVG filter or recover pixel values.

## Run

```sh
python3 subnormal_stealing/serve.py
```

## Overview
The attacker page embeds the victim page in an iframe and confirms it can’t read the victim’s pixels directly. It then isolates one pixel by scrolling the iframe behind a 1×1 window and scaling it, and does timing by measuring the interval between two requestAnimationFrame callbacks with performance.now().
Since SVG filter isn’t applied yet, that timing doesn’t depend on the pixel’s color so right now it recovers nothing

## Cross-origin setup: serve.py
http://127.0.0.1:8000 — the attacker origin. Serves `index.html` and `attacker.js`(controls and logic)
http://127.0.0.1:8001 — the victim origin. Serves the secret 8×8 image and `truth.json`. loaded in attacker's iframe

The secret: `pixels = [0]*32 + [1]*32`, `secrets.SystemRandom().shuffle(...)` — balanced image (in this case 32 black and 32 white), fixed for the life of the server

Victim: victim_html(pixels) — builds a small self-contained SVG document. One SVG unit maps to one CSS pixel. After load, it does `parent.postMessage('fixture-ready', 'http://127.0.0.1:8000')` to the attacker origin


## Attacker logic: index.html, attacker.js
HTML/CSS:
`#victim` is the `<iframe>`, sized 8×8 CSS px
`#pixel` wraps it, a 1px × 1px with overflow: hidden and transform: scale(200)
`#inspection` is the 200×200 outlined box it all sits in

attacker.js:
- `selectPixel()`: Sets #victim.style.left = -x px and top = -y px. Line up (x, y) with the top-left corner, then the scale() enlarges that single clipped pixel to the chosen 100/200/300 px square
- `sample()`: measures the interval between two consecutive `requestAnimationFrame` callbacks
1. Measure black/white baseline is for calibration later
2. Scan timing goes through each pixel and time each one and compare with T (But right now no filters are applied)




## Notes
When running Scan timing only, the blue grid cells mean sampled, and `?` means color unknown
We can add some export to CSV or JSON for timing data if needed in the future

