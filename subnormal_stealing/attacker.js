const $ = id => document.getElementById(id);
const frame = () => new Promise(resolve => requestAnimationFrame(resolve));
const mean = values => values.reduce((sum, value) => sum + value, 0) / values.length;
let busy = false;
let stopped = false;
let loaded = false;
const cells = Array.from({length: 64}, () => {
  const cell = document.createElement('span');
  cell.textContent = '?';
  $('grid').appendChild(cell);
  return cell;
});

function settings() {
  return {area: Number($('area').value),
    samples: Number($('samples').value)};
}

function selectPixel() {
  for (const axis of ['x', 'y']) {
    $(axis).value = Math.max(0, Math.min(7, Math.floor(Number($(axis).value) || 0)));
  }
  const x = Number($('x').value), y = Number($('y').value);
  $('victim').style.left = `${-x}px`;
  $('victim').style.top = `${-y}px`;
  cells.forEach((cell, i) => cell.classList.toggle('selected', i === y * 8 + x));
  const area = settings().area;
  $('pixel').style.transform = `scale(${area})`;
  $('inspection').style.width = $('inspection').style.height = `${area}px`;
}

function controls() {
  for (const id of ['x', 'y', 'area', 'samples']) $(id).disabled = busy;
  $('scan').disabled = $('calibrate').disabled = busy || !loaded;
  $('stop').disabled = !busy;
}

function loadVictim() {
  loaded = false;
  controls();
  $('boundary').textContent = 'Loading fixture…';
  $('victim').src = 'http://127.0.0.1:8001/';
  cells.forEach(cell => { cell.textContent = '?'; cell.classList.remove('sampled'); });
  $('status').textContent = 'Not calibrated. Baseline measurements only.';
}

window.addEventListener('message', event => {
  const origin = 'http://127.0.0.1:8001';
  if (event.source !== $('victim').contentWindow || event.origin !== origin || event.data !== 'fixture-ready') return;
  let readable = false;
  try { readable = Boolean($('victim').contentWindow.document.querySelector('svg')); }
  catch (error) { if (error.name !== 'SecurityError') throw error; }
  loaded = !readable;
  $('boundary').textContent = `Cross-origin check: DOM ${readable ? 'unexpectedly readable — stopped' : 'access blocked, as expected'}.`;
  selectPixel();
  controls();
});

async function sample() {
  if (stopped || document.hidden) throw new Error('Stopped.');
  await frame();
  const start = performance.now();
  await frame();
  if (stopped || document.hidden) throw new Error('Stopped.');
  return performance.now() - start;
}

async function run(kind) {
  if (busy || !loaded) return;
  const count = settings().samples;
  if (!Number.isInteger(count) || count < 1 || count > 100) {
    $('status').textContent = 'Choose 1–100 samples per pixel.';
    return;
  }
  const rect = $('inspection').getBoundingClientRect();
  if (rect.top < 0 || rect.left < 0 || rect.bottom > innerHeight || rect.right > innerWidth) {
    $('status').textContent = 'Move the entire inspection area into view before measuring.';
    return;
  }
  busy = true;
  stopped = false;
  controls();
  const timings = [];
  try {
    $('status').textContent = 'Warming up…';
    for (let i = 0; i < 5; i++) await sample();
    if (kind === 'baseline') {
      const colors = {black: [], white: []};
      $('patch').style.display = 'block';
      for (let i = 0; i < count; i++) {
        for (const color of i % 2 ? ['white', 'black'] : ['black', 'white']) {
          $('patch').style.background = color;
          const ms = await sample();
          colors[color].push(ms);
        }
      }
      const average = color => mean(colors[color]);
      $('summary').textContent = `Black: ${average('black').toFixed(2)} ms; white: ${average('white').toFixed(2)} ms.`;
      $('status').textContent = 'Baseline measurement complete.';
    } else {
      cells.forEach(cell => { cell.textContent = '?'; cell.classList.remove('sampled'); });
      for (let i = 0; i < 64; i++) {
        $('x').value = i % 8;
        $('y').value = Math.floor(i / 8);
        selectPixel();
        $('status').textContent = `Measuring pixel ${i + 1}/64…`;
        for (let j = 0; j < count; j++) {
          timings.push(await sample());
        }
        cells[i].classList.add('sampled');
      }
      $('summary').textContent = `${timings.length} samples; mean ${mean(timings).toFixed(2)} ms.`;
      $('status').textContent = 'Scan complete. ';
    }
  } catch (error) {
    $('status').textContent = error.message;
  } finally {
    $('patch').style.display = 'none';
    busy = false;
    controls();
  }
}

for (const id of ['x', 'y', 'area']) $(id).onchange = selectPixel;
$('calibrate').onclick = () => run('baseline');
$('scan').onclick = () => run('scan');
$('stop').onclick = () => { stopped = true; };
document.addEventListener('visibilitychange', () => { if (document.hidden) stopped = true; });
loadVictim();
