import os
from dotenv import load_dotenv
load_dotenv()
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

apiHost = os.getenv('API_HOST') or ''
mqttBrokerUrl = os.getenv('MQTT_BROKER_URL') or ''
mqttClientId = os.getenv('MQTT_CLIENT_ID') or ''
mqttUsername = os.getenv('MQTT_USERNAME') or ''
mqttPassword = os.getenv('MQTT_PASSWORD') or ''
mqttRegistrationTopic = os.getenv('MQTT_REGISTRATION_TOPIC') or ''
mqttAckTopic = os.getenv('MQTT_ACK_TOPIC') or ''
mqttQos = os.getenv('MQTT_QOS') or ''

print(f'-DAPI_HOST="\\"{apiHost}\\""')
print(f'-DMQTT_BROKER_URL="\\"{mqttBrokerUrl}\\""')
print(f'-DMQTT_CLIENT_ID="\\"{mqttClientId}\\""')
print(f'-DMQTT_USERNAME="\\"{mqttUsername}\\""')
print(f'-DMQTT_PASSWORD="\\"{mqttPassword}\\""')
print(f'-DMQTT_REGISTRATION_TOPIC="\\"{mqttRegistrationTopic}\\""')
print(f'-DMQTT_ACK_TOPIC="\\"{mqttAckTopic}\\""')
print(f'-DMQTT_QOS={mqttQos}')
