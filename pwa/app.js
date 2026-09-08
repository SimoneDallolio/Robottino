const SERVICE_UUID = '7f7b0001-8b3a-4f65-9d39-2c8c5a7e1001';
const CONFIG_UUID = '7f7b0002-8b3a-4f65-9d39-2c8c5a7e1001';

const $ = (id) => document.getElementById(id);
const state = { device: null, characteristic: null, latitude: null, longitude: null, epoch: Date.now() };

function feedback(message, error = false) {
  $('feedback').textContent = message;
  $('feedback').classList.toggle('error', error);
}

function setConnected(connected) {
  $('connectionDot').classList.toggle('connected', connected);
  $('connectionDot').setAttribute('aria-label', connected ? 'Connesso' : 'Non connesso');
}

async function connect() {
  if (!('bluetooth' in navigator)) throw new Error('Web Bluetooth non supportato. Usa Chrome o Edge su Android/desktop.');
  feedback('Seleziona il tuo Robottino...');
  state.device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'Robottino-' }],
    optionalServices: [SERVICE_UUID]
  });
  state.device.addEventListener('gattserverdisconnected', () => setConnected(false));
  const server = await state.device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE_UUID);
  state.characteristic = await service.getCharacteristic(CONFIG_UUID);
  await state.characteristic.startNotifications();
  state.characteristic.addEventListener('characteristicvaluechanged', handleResponse);
  setConnected(true);
}

function handleResponse(event) {
  const message = new TextDecoder().decode(event.target.value);
  try {
    const result = JSON.parse(message);
    feedback(result.ok ? 'Configurazione salvata. BLE disattivato.' : `Errore: ${result.error}`, !result.ok);
  } catch {
    feedback(message);
  }
}

function detectLocation() {
  if (!navigator.geolocation) {
    feedback('Geolocalizzazione non disponibile.', true);
    return;
  }
  navigator.geolocation.getCurrentPosition((position) => {
    state.latitude = position.coords.latitude;
    state.longitude = position.coords.longitude;
    $('locationText').textContent = `${state.latitude.toFixed(5)}, ${state.longitude.toFixed(5)}`;
  }, () => feedback('Permesso GPS negato o posizione non disponibile.', true), { enableHighAccuracy: true, timeout: 10000 });
}

function syncTime() {
  state.epoch = Date.now();
  $('timeText').textContent = new Date(state.epoch).toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit' });
}

async function sendConfiguration() {
  try {
    if (!state.characteristic) await connect();
    const pin = $('pin').value.trim();
    const ssid = $('ssid').value.trim();
    const pass = $('wifiPassword').value;
    if (!/^\d{4}$/.test(pin)) throw new Error('Il PIN deve contenere 4 cifre.');
    if (!ssid) throw new Error('Inserisci il nome della rete Wi-Fi.');
    if (state.latitude === null || state.longitude === null) throw new Error('Rileva prima la posizione GPS.');

    const payload = { pin, ssid, pass, time: Math.floor(state.epoch / 1000), lat: state.latitude, lon: state.longitude };
    const bytes = new TextEncoder().encode(JSON.stringify(payload));
    await state.characteristic.writeValueWithResponse(bytes);
    feedback('Configurazione inviata, attendo conferma...');
  } catch (error) {
    feedback(error.message || 'Errore BLE.', true);
  }
}

$('locationButton').addEventListener('click', detectLocation);
$('timeButton').addEventListener('click', syncTime);
$('sendButton').addEventListener('click', sendConfiguration);
syncTime();

if ('serviceWorker' in navigator) window.addEventListener('load', () => navigator.serviceWorker.register('sw.js'));
