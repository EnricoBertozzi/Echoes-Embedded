export class WifiManager {
  wifis = [];
  status = null;
  #listeners = {
    fetch: [],
    add: [],
    update: [],
  };
  #evt;

  constructor() {
    this.#evt = new EventSource('/events');
  }

  setup() {
    this.#fetchWifi();
    this.#evt.addEventListener('status', e => this.#fetchStatus(e));
    this.#evt.addEventListener('list', e => this.#fetchWifi(e));
  }

  setWifis(wifis) {
    this.wifis = wifis;
    this.#notify('fetch');
  }

  async #fetchWifi(evt) {
    const data = JSON.parse(evt.data);
    if (data.length === 0)
      return;
    this.wifis = data;
    this.#notify('fetch');
  }

  async #fetchStatus(evt) {
    const data = JSON.parse(evt.data);
    if (!data.ssid)
      return;

    const networkIndex = this.wifis.findIndex(w => w.ssid === data.ssid);
    if (networkIndex !== -1)
      this.wifis.splice(networkIndex, 1);
    this.status = data;
    this.#notify('update');
  }

  async connectToWifi(wifiConnectDto) {
    try {
      await this.#_connectToWifi(wifiConnectDto);
      this.#markStatus(wifiConnectDto, 1);
    } catch (err) {
      console.error(err);
      this.#markStatus(wifiConnectDto, 3);
      throw new Error('Erro ao conectar');
    }
  }

  async #_connectToWifi(wifiDto) {
    const response = await fetch('/wifi-setup', {
      method: "POST",
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(wifiDto),
    });
    if (!response.ok)
      throw new Error('Erro ao conectar');
  }

  #markStatus(wifiDto, status) {
    if (this.status && this.status !== wifiDto.ssid) {
      this.wifis.splice(0, 1);
      this.status = null;
    }
    const i = this.wifis.findIndex(w => w.ssid === wifiDto.ssid);
    if (i >= 0) {
      this.wifis[i].status = status;
      this.#notify('update');
      return;
    }

    const wifi = {
      ssid: wifiDto.ssid,
      status: status,
      secure: wifiDto.pass !== '',
      rssi: wifiDto.rssi,
    }
    this.status = wifi;
    this.#notify('update');
  }

  addListener(event, cb) {
    if (!this.#listeners[event])
      return;
    this.#listeners[event].push(cb);
  }

  removeListener(event, cb) {
    if (!this.#listeners[event])
      return;
    const i = this.#listeners[event].findIndex(i => i === cb);
    if (i >= 0);
    this.#listeners[event].splice(i, 1);
  }

  #notify(event) {
    if (!this.#listeners[event])
      return;
    for (const cb of this.#listeners[event]) {
      cb();
    }
  }
}
