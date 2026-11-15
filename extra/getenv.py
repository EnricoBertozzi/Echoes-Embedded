import os
import sys

try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    print(
        "aviso: python-dotenv nao instalado; usando apenas variaveis do sistema",
        file=sys.stderr,
    )

pioEnv = os.getenv('PIO_ENV') or ''
if pioEnv.lower() == "test":
    print('-DTEST_BUILD')
if pioEnv.lower() == "dev":
    print('-DDEV_BUILD')
if pioEnv.lower() == "verbose":
    print('-DCORE_DEBUG_LEVEL=5')
    print('-DDEV_BUILD')
    print('-DLWIP_DEBUG=1')
    print('-DLWIP_HTTPD_DEBUG=LWIP_DBG_ON')
    print('-DLWIP_TCP_DEBUG=LWIP_DBG_ON')
    print('-DLWIP_IP_DEBUG=LWIP_DBG_ON')
    print('-DHTTP_DEBUG=1')


def string_flag(name):
    value = os.getenv(name) or ''
    if value:
        print(f'-D{name}="\\"{value}\\""')


def int_flag(name):
    value = os.getenv(name) or ''
    if value.isdigit():
        print(f'-D{name}={value}')


string_flag('API_HOST')
string_flag('MQTT_BROKER_URL')
string_flag('MQTT_CLIENT_ID')
string_flag('MQTT_USERNAME')
string_flag('MQTT_PASSWORD')
string_flag('MQTT_REGISTRATION_TOPIC')
string_flag('MQTT_ACK_TOPIC')
int_flag('MQTT_QOS')
