# Introdução

O uso de animais reais na medicina veterinária é um tema debatido entre os profissionais da área. Como forma preservar o bem-estar animal, normas e alternativas foram desenvolvidas com o objetivo de reduzir e substituir o uso de animais durante atividades pedagógicas e científicas, além de aprimorar os métodos já utilizados.

Nesse contexto, o uso de simuladores didáticos são eficazes no treinamento de habilidades clínicas, contribuindo para a redução da ansiedade e permitindo a repetição ilimitada das técnicas.

O projeto de um conjunto consiste no desenvolvimento de um simulador canino de baixo custo para o estudo de ausculta pulmonar e cardíaca, utilizando de componentes relacionados a Desenvolvimento Mobile, Web e Sistemas Embarcados com Internet das Coisas (IoT).  

# Tecnologias
* C++.
* ESP-IDF

# Reprodução offline e sincronização

- Após um `204` de `/api/v1/devices/me/playback-authorization`, a permissão é salva na NVS para o dispositivo e a URL da API atuais.
- Com permissão salva e MP3 sincronizado, os sensores reproduzem offline, inclusive após reiniciar, sem consulta HTTP ao pressionar.
- A task de rede verifica a autorização ao conectar e a cada hora. Falha de rede, timeout ou erro temporário mantém a decisão anterior.
- `401` ou `403` na autorização ou no manifesto revoga a permissão salva e interrompe a reprodução. Um dispositivo sem autorização anterior permanece bloqueado até receber `204`.
- Cenários continuam sendo consultados por HTTP a cada 60 segundos quando conectado; offline é usado o último cenário baixado completamente.
- No contrato atual do Echoes-Server, MQTT serve ao cadastro e seu ACK. Não há push MQTT de revogação ou mudança de cenário.
