export class WifiConnectDto {
  constructor(ssid, password, ip, gateway, subnet) {
    this.ssid = ssid;
    this.password = password;
    this.ip = ip;
    this.gateway = gateway;
    this.subnet = subnet;
  }
}
