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
    // First load only: fetch the last scan list so the page renders right
    // away, before the first SSE event arrives. Updates come via SSE.
    this.#fetchWifiList();
    this.#evt.addEventListener('status', e => this.#fetchStatus(e));
    this.#evt.addEventListener('list', e => this.#fetchWifi(e));
  }

  setWifis(wifis) {
    this.wifis = wifis;
    this.#notify('fetch');
  }

  async #fetchWifiList() {
    try {
      const response = await fetch('/wifi-list');
      if (!response.ok)
        return;
      this.setWifis(await response.json());
    } catch (err) {
      console.error(err);
    }
  }

  async #fetchWifi(evt) {
    const data = JSON.parse(evt.data);
    this.wifis = data;
    this.#notify('fetch');
  }

  async #fetchStatus(evt) {
    const data = JSON.parse(evt.data);
    if (!data.ssid)
      return;

    if (data.status === 0) {
      // Disconnected: drop the status card, the network returns to the list.
      if (this.status && this.status.ssid === data.ssid)
        this.status = null;
      this.#notify('update');
      return;
    }

    const networkIndex = this.wifis.findIndex(w => w.ssid === data.ssid);
    let wifi = data;
    if (networkIndex !== -1) {
      const known = this.wifis[networkIndex];
      // Keep signal/lock info from the scan while there is no real value.
      wifi = {
        ...data,
        rssi: data.rssi || known.rssi,
        secure: data.secure || known.secure,
      };
      this.wifis.splice(networkIndex, 1);
    }
    this.status = wifi;
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
    if (this.status && this.status.ssid !== wifiDto.ssid) {
      // The previous status card goes back to the list.
      const previous = { ...this.status, status: 0 };
      if (!this.wifis.some(w => w.ssid === previous.ssid))
        this.wifis.unshift(previous);
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
      secure: wifiDto.password !== '',
      rssi: 0,
    };
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
    if (i >= 0)
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
