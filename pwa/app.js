const SERVICE_UUID = '7f7b0001-8b3a-4f65-9d39-2c8c5a7e1001';
const CONFIG_UUID = '7f7b0002-8b3a-4f65-9d39-2c8c5a7e1001';
const STATUS_UUID = '7f7b0003-8b3a-4f65-9d39-2c8c5a7e1001';

const $ = (id) => document.getElementById(id);
const state = {
  device: null,
  characteristic: null,
  statusCharacteristic: null,
  firstBoot: false,
  pin: '',
  latitude: null,
  longitude: null,
  epoch: Date.now(),
  responseWaiter: null
};

function showFeedback(elementId, message, error = false) {
  const element = $(elementId);
  element.textContent = message;
  element.classList.toggle('error', error);
}

function setConnected(connected) {
  $('connectionDot').classList.toggle('connected', connected);
  $('connectionDot').setAttribute('aria-label', connected ? 'Connesso' : 'Non connesso');
}

async function connectToRobot() {
  try {
    if (!('bluetooth' in navigator)) throw new Error('Web Bluetooth non supportato da questo browser.');
    showFeedback('welcomeFeedback', 'Seleziona il tuo Robottino...');
    state.device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: 'Robottino-' }],
      optionalServices: [SERVICE_UUID]
    });
    state.device.addEventListener('gattserverdisconnected', () => setConnected(false));
    const server = await state.device.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);
    state.characteristic = await service.getCharacteristic(CONFIG_UUID);
    state.statusCharacteristic = await service.getCharacteristic(STATUS_UUID);
    await state.characteristic.startNotifications();
    state.characteristic.addEventListener('characteristicvaluechanged', handleResponse);
    const statusBytes = await state.statusCharacteristic.readValue();
    const status = JSON.parse(new TextDecoder().decode(statusBytes));
    state.firstBoot = status.firstBoot === true;
    setConnected(true);
    $('welcomeScreen').hidden = true;
    $('appHeader').hidden = false;
    $('accessPanel').hidden = false;
    $('appFooter').hidden = false;
    $('firstBootPanel').hidden = !state.firstBoot;
    showFeedback('accessFeedback', state.firstBoot
      ? 'Primo avvio: il PIN iniziale è 0000 e devi sostituirlo.'
      : 'Inserisci il PIN per continuare.');
  } catch (error) {
    showFeedback('welcomeFeedback', error.message || 'Connessione BLE fallita.', true);
  }
}

function handleResponse(event) {
  const message = new TextDecoder().decode(event.target.value);
  try {
    const result = JSON.parse(message);
    if (state.responseWaiter) {
      const waiter = state.responseWaiter;
      state.responseWaiter = null;
      waiter(result);
      return;
    }
    if (result.ok) {
      showFeedback('feedback', 'Configurazione salvata. BLE disattivato.');
    } else {
      showFeedback('accessFeedback', result.error || 'PIN non valido.', true);
      showFeedback('feedback', result.error || 'Configurazione rifiutata.', true);
    }
  } catch {
    showFeedback('feedback', message);
  }
}

function writeAndWait(payload) {
  return new Promise(async (resolve, reject) => {
    state.responseWaiter = resolve;
    try {
      await state.characteristic.writeValueWithResponse(new TextEncoder().encode(JSON.stringify(payload)));
      setTimeout(() => {
        if (!state.responseWaiter) return;
        state.responseWaiter = null;
        reject(new Error('Nessuna risposta dal Robottino.'));
      }, 3000);
    } catch (error) {
      state.responseWaiter = null;
      reject(error);
    }
  });
}

async function verifyAccess() {
  const pin = $('pin').value.trim();
  if (!/^\d{4}$/.test(pin)) throw new Error('Il PIN deve contenere 4 cifre.');

  if (state.firstBoot) {
    const newPin = $('newPin').value.trim();
    if (pin !== '0000') throw new Error('Al primo avvio devi usare il PIN iniziale 0000.');
    if (!/^\d{4}$/.test(newPin) || newPin === '0000') throw new Error('Scegli un nuovo PIN di 4 cifre diverso da 0000.');
    state.pin = pin;
  } else {
    const result = await writeAndWait({ action: 'auth', pin });
    if (!result.ok) throw new Error(result.error || 'PIN non valido.');
    state.pin = pin;
  }

  $('accessPanel').hidden = true;
  $('setupContent').hidden = false;
  $('appHeader').scrollIntoView({ behavior: 'smooth', block: 'start' });
  showFeedback('feedback', state.firstBoot ? 'PIN iniziale verificato: completa la nuova configurazione.' : 'Accesso verificato.');
}

function detectLocation() {
  if (!navigator.geolocation) return showFeedback('feedback', 'Geolocalizzazione non disponibile.', true);
  navigator.geolocation.getCurrentPosition((position) => {
    state.latitude = position.coords.latitude;
    state.longitude = position.coords.longitude;
    $('locationText').textContent = `${state.latitude.toFixed(5)}, ${state.longitude.toFixed(5)}`;
  }, () => showFeedback('feedback', 'Permesso GPS negato o posizione non disponibile.', true), { enableHighAccuracy: true, timeout: 10000 });
}

function syncTime() {
  state.epoch = Date.now();
  $('timeText').textContent = new Date(state.epoch).toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit' });
}

async function sendConfiguration() {
  try {
    const ssid = $('ssid').value.trim();
    const pass = $('wifiPassword').value;
    if (!ssid) throw new Error('Inserisci il nome della rete Wi-Fi.');
    if (state.latitude === null || state.longitude === null) throw new Error('Rileva prima la posizione GPS.');
    const payload = {
      pin: state.pin,
      newPin: state.firstBoot ? $('newPin').value.trim() : '',
      ssid,
      pass,
      time: Math.floor(state.epoch / 1000),
      lat: state.latitude,
      lon: state.longitude
    };
    const bytes = new TextEncoder().encode(JSON.stringify(payload));
    await state.characteristic.writeValueWithResponse(bytes);
    showFeedback('feedback', 'Configurazione inviata, attendo conferma...');
  } catch (error) {
    showFeedback('feedback', error.message || 'Errore BLE.', true);
  }
}

$('connectButton').addEventListener('click', connectToRobot);
$('unlockButton').addEventListener('click', () => verifyAccess().catch((error) => showFeedback('accessFeedback', error.message, true)));
$('locationButton').addEventListener('click', detectLocation);
$('timeButton').addEventListener('click', syncTime);
$('sendButton').addEventListener('click', sendConfiguration);
$('accessPanel').hidden = true;
$('setupContent').hidden = true;
syncTime();

if ('serviceWorker' in navigator) window.addEventListener('load', () => navigator.serviceWorker.register('sw.js'));
