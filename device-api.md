# API do Dispositivo

Documentação das rotas destinadas ao dispositivo (firmware/app) autenticado por JWT autoassinado.

> **Escopo:** somente rotas do dispositivo.
> As rotas de administração de animais estão documentadas em
> [`admin-animal-api.md`](./admin-animal-api.md).

## Base URL

`http://localhost:8080` ou o valor configurado em `SERVER_PORT`.

## 1. Modelo `Device`

O dispositivo é uma entidade própria, separada do animal.

| Campo | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `id` | `UUID` | Sim | Identificador do dispositivo |
| `name` | `String` | Não | Nome de exibição |
| `mac` | `String` | Sim | MAC único do dispositivo |
| `publicKey` | `String` | Sim | Chave pública EC P-256 em PEM |
| `animal` | `AnimalModel` | Não | Animal associado ao dispositivo |

O animal representa apenas o modelo/protótipo (por exemplo, cachorro ou gato). MAC e chave pública
pertencem ao dispositivo, não ao animal.

### Cadastro direto no banco (alternativo)

Não existem endpoints de criação, atualização ou exclusão de dispositivo. O caminho normal de
provisionamento é o **autocadastro via MQTT** (seção 2); a inserção direta no banco continua
disponível para administração, recuperação ou cadastro manual.

No MySQL, o Hibernate mapeia `UUID` como `binary(16)`. Um exemplo de inserção:

```sql
INSERT INTO device_tb (id, name, mac, public_key, animal)
VALUES (
    UNHEX(REPLACE(UUID(), '-', '')),
    'Echoes Dev Kit 01',
    'AA:BB:CC:DD:EE:01',
    '-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE...
-----END PUBLIC KEY-----',
    UNHEX(REPLACE('9f2c1d3e-4b5a-6c7d-8e9f-0a1b2c3d4e5f', '-', ''))
);
```

Para cadastrar um dispositivo **sem animal associado**, use `NULL` na coluna `animal`:

```sql
INSERT INTO device_tb (id, name, mac, public_key, animal)
VALUES (
    UNHEX(REPLACE(UUID(), '-', '')),
    'Echoes Dev Kit 02',
    'AA:BB:CC:DD:EE:02',
    '-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE...
-----END PUBLIC KEY-----',
    NULL
);
```

Observações:

- A tabela `device_tb` é criada pelo Hibernate com `ddl-auto=update`.
- O `mac` é único.
- A `public_key` é o texto PEM completo, incluindo `-----BEGIN PUBLIC KEY-----` e
  `-----END PUBLIC KEY-----`.
- A chave deve ser EC P-256 (SPKI), compatível com **ES256**.
- A coluna `animal` aceita `NULL`.
- Ao excluir um animal, a associação do dispositivo é anulada pela FK
  (`ON DELETE SET NULL`); o dispositivo permanece cadastrado.

## 2. Autocadastro via MQTT

O dispositivo se conecta ao broker MQTT e publica o pedido de cadastro no tópico `register`. O
servidor mantém um cliente inscrito nesse tópico, valida a assinatura e cadastra o dispositivo
automaticamente. Não existe endpoint HTTP de autocadastro.

### 2.1 Configuração do servidor

O cliente MQTT do servidor é configurado pelas variáveis de ambiente:

| Variável | Padrão | Descrição |
|---|---|---|
| `MQTT_ENABLED` | `true` | Liga/desliga o cliente MQTT |
| `MQTT_BROKER_URL` | `tcp://localhost:1883` | URL do broker (`tcp://` ou `ssl://`) |
| `MQTT_CLIENT_ID` | `echoes-server-registration` | Client ID único no broker |
| `MQTT_USERNAME` | vazio | Usuário do broker |
| `MQTT_PASSWORD` | vazio | Senha do broker |
| `MQTT_REGISTRATION_TOPIC` | `register` | Tópico de entrada |
| `MQTT_ACK_TOPIC` | `register/ack/{mac}` | Tópico de resposta; `{mac}` é substituído |
| `MQTT_QOS` | `1` | QoS da inscrição e das respostas |
| `MQTT_CLEAN_SESSION` | `true` | Sessão limpa no broker |
| `MQTT_CONNECTION_TIMEOUT_SECONDS` | `10` | Timeout de conexão |
| `MQTT_KEEP_ALIVE_SECONDS` | `60` | Keep-alive |
| `MQTT_RECONNECT_DELAY_SECONDS` | `5` | Intervalo entre tentativas |

Se o broker estiver indisponível no startup, a aplicação continua subindo e o cliente tenta
reconectar em segundo plano. Nos testes automatizados, `mqtt.enabled=false` desliga o cliente.

### 2.2 Payload de entrada

Publicação do dispositivo no tópico `register`:

```json
{
  "mac": "AA:BB:CC:DD:EE:01",
  "public_key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE...\n-----END PUBLIC KEY-----",
  "signature": "MEUCIQ...",
  "name": "Echoes Dev Kit 01"
}
```

| Campo | Obrigatório | Descrição |
|---|---|---|
| `mac` | Sim | MAC do dispositivo |
| `public_key` | Sim | Chave pública EC P-256 em PEM (SPKI) |
| `signature` | Sim | Assinatura da mensagem canônica |
| `name` | Não | Nome de exibição; se ausente, usa-se o MAC |

O campo `public_key` também é aceito como `publicKey`, e `signature` também como `sig`. Campos
desconhecidos são ignorados.

#### Como enviar `public_key` no JSON

No JSON, as quebras de linha da PEM devem ser enviadas como a sequência de escape `\n`
(barra + n), não como quebras de linha físicas dentro da string. Exemplo:

```json
"public_key": "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEoIriSdotMT4T6B370E6VlCiy5pQtY+PFnfqTsdUdnsExWwJXnc3wXTjlS/DOp1H2lNJxTXel/1J20r1lvAIYuA==\n-----END PUBLIC KEY-----"
```

Se você estiver montando o JSON em código (JavaScript, Python, etc.), basta passar a PEM como
uma string normal e deixar a biblioteca de JSON (`JSON.stringify`, `json.dumps`, etc.) fazer o
escape automático de `\n`.

#### Como enviar `signature`

A assinatura pode ser enviada em **Base64 padrão**, **Base64 URL-safe** ou **hexadecimal**.
O servidor aceita os formatos **JOSE** (`R || S`, 64 bytes) e **ASN.1/DER**.

### 2.3 Mensagem assinada

A assinatura deve ser ECDSA P-256 com SHA-256 sobre os bytes UTF-8 da mensagem canônica abaixo.
`mac` e `public_key` são usados **sem espaços nas extremidades** e separados por `\n`:

```text
echoes-device-registration-v1
<mac>
<public_key>
```

Exemplo:

```text
echoes-device-registration-v1
AA:BB:CC:DD:EE:01
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE...
-----END PUBLIC KEY-----
```

### 2.4 Formato da assinatura

- Algoritmo: **ECDSA P-256/SHA-256** (`ES256`).
- Codificação: Base64 padrão, Base64 URL-safe ou hexadecimal.
- Formato do valor: **JOSE** (`R || S`, 64 bytes) ou **ASN.1/DER**. O formato DER gerado por
  `mbedtls_ecdsa_sign` é aceito diretamente, sem conversão para JOSE.

### 2.5 Resposta

O servidor publica o resultado em `register/ack/{mac}` (configurável em `MQTT_ACK_TOPIC`):

```json
{
  "status": "REGISTERED",
  "mac": "AA:BB:CC:DD:EE:01",
  "deviceId": "7c9e6679-7425-40de-944b-e07fc1f90ae7",
  "message": "Dispositivo registrado"
}
```

Status possíveis:

| Status | Quando ocorre |
|---|---|
| `REGISTERED` | Dispositivo novo cadastrado |
| `ALREADY_REGISTERED` | MAC já existe com a mesma chave pública |
| `REJECTED` | Payload, chave ou assinatura inválida; ou MAC já existe com outra chave |

Exemplo de rejeição:

```json
{
  "status": "REJECTED",
  "mac": "AA:BB:CC:DD:EE:01",
  "deviceId": null,
  "message": "Assinatura do dispositivo inválida"
}
```

### 2.6 Regras de cadastro

- O dispositivo é criado com `animal = NULL`; a associação com um animal continua sendo feita
  pela administração/dashboard ou diretamente no banco.
- Um MAC já cadastrado **nunca** tem a chave pública sobrescrita. O reenvio do mesmo MAC com a
  mesma chave é idempotente; com chave diferente é rejeitado.
- A assinatura prova a posse da chave privada correspondente à chave pública enviada, mas não
  autentica o MAC perante o broker. Restrinja a publicação em `register` com autenticação, TLS e
  ACLs no broker; qualquer cliente autorizado a publicar pode tentar cadastrar um MAC ainda livre.
- O cadastro via MQTT não cria pontos de ausculta nem cenários.

### 2.7 Exemplo de geração da assinatura (Node.js)

```js
const crypto = require('crypto');
const fs = require('fs');

const mac = 'AA:BB:CC:DD:EE:FF';
const publicKeyPem = fs.readFileSync('pubkey.pem', 'utf8');
const privateKeyPem = fs.readFileSync('private.pem', 'utf8');

// Mensagem canônica assinada pelo dispositivo
const message = `echoes-device-registration-v1\n${mac}\n${publicKeyPem.trim()}`;

// Assina com ECDSA P-256/SHA-256 (DER)
const signatureDer = crypto.sign(null, Buffer.from(message), privateKeyPem);

// Envia em Base64
const signatureBase64 = signatureDer.toString('base64');
```

### 2.8 Exemplo de publicação no tópico `register` (Node.js + MQTT)

```js
const mqtt = require('mqtt');

const client = mqtt.connect('ssl://seu-broker:8883', {
  username: 'seu_usuario',
  password: 'sua_senha',
  clientId: 'meu-dispositivo-01',
});

client.on('connect', () => {
  const payload = JSON.stringify({
    mac: 'AA:BB:CC:DD:EE:FF',
    public_key: publicKeyPem,
    signature: signatureBase64,
    name: 'Meu Dispositivo',
  });

  client.subscribe('register/ack/AA:BB:CC:DD:EE:FF', { qos: 1 });
  client.publish('register', payload, { qos: 1 });
});

client.on('message', (topic, message) => {
  console.log('Resposta do servidor:', message.toString());
  client.end();
});
```

### 2.9 Exemplo de fluxo do firmware

```text
1. Conecta ao broker MQTT (TLS + credenciais, se configurado).
2. Publica em "register" o JSON com mac, public_key, signature e name opcional.
3. Inscreve-se em "register/ack/{mac}" e aguarda o resultado.
4. Em caso de ALREADY_REGISTERED, segue para gerar o JWT de acesso.
5. Em caso de REJECTED, corrige o payload/assinatura ou verifica o cadastro com o administrador.
```

## 3. Autenticação

O dispositivo gera o próprio JWT e o envia no header:

```http
Authorization: Bearer <device_jwt>
```

### Estrutura do token

Header:

```json
{
  "alg": "ES256",
  "typ": "JWT"
}
```

Payload:

```json
{
  "iss": "echoes-device",
  "mac": "AA:BB:CC:DD:EE:01",
  "exp": 1730000000
}
```

Regras:

- `iss` deve ser exatamente `echoes-device`.
- `mac` deve ser o MAC cadastrado para o **dispositivo**.
- `exp` é um timestamp Unix em **segundos**.
- A assinatura deve ser **ES256** (ECDSA P-256), usando a chave privada correspondente à
  `publicKey` cadastrada no dispositivo.
- A assinatura ECDSA deve estar no formato **JOSE** (`R || S`, 64 bytes para P-256), não no formato
  ASN.1/DER. Bibliotecas de JWT normalmente já fazem essa conversão; se o firmware assinar
  manualmente, aplique a conversão.
- O servidor localiza o dispositivo pelo `mac`, valida a assinatura com a `publicKey` do
  dispositivo e verifica o `exp`.
- Tokens expirados ou com assinatura inválida retornam `401`.
- Não existe endpoint de emissão, renovação ou revogação de token. O dispositivo deve gerar um novo
  token quando o atual expirar.
- Não há lista negra/revogação no servidor.

### Exemplo de geração (pseudocódigo)

```text
header  = {"alg":"ES256","typ":"JWT"}
payload = {"iss":"echoes-device","mac":"AA:BB:CC:DD:EE:01","exp":agora + 300}

jwt = base64url(header) + "." + base64url(payload)
jwt = jwt + "." + ES256_sign(jwt, chave_privada_dispositivo)
```

## 4. Resumo das rotas

Todas as rotas abaixo exigem `Authorization: Bearer <device_jwt>`.

| Método | Rota | Descrição |
|---|---|---|
| `GET` | `/devices/me` | Retorna os dados do dispositivo autenticado |
| `GET` | `/points/me` | Lista os pontos de ausculta do animal associado |
| `GET` | `/points/{id}` | Busca um ponto do animal associado |
| `POST` | `/points` | Cria um ponto para o animal associado |
| `PATCH` | `/points/{id}` | Atualiza a posição de um ponto do animal associado |
| `DELETE` | `/points/{id}` | Remove um ponto do animal associado |
| `GET` | `/api/v1/scenarios/me` | Lista os cenários/áudios do animal associado |
| `GET` | `/api/v1/scenarios?id={pointId}` | Lista os cenários de um ponto do animal associado |
| `GET` | `/api/v1/scenarios/search?name={name}` | Busca cenários do animal associado por nome |
| `GET` | `/api/v1/scenarios/{id}` | Busca um cenário do animal associado |

## 5. Modelos

### DeviceInfo

```json
{
  "id": "7c9e6679-7425-40de-944b-e07fc1f90ae7",
  "name": "Echoes Dev Kit 01",
  "mac": "AA:BB:CC:DD:EE:01",
  "animalId": "9f2c1d3e-4b5a-6c7d-8e9f-0a1b2c3d4e5f",
  "animalName": "Cachorro"
}
```

Se o dispositivo não tiver animal associado, `animalId` e `animalName` serão `null`.

> A `publicKey` não é retornada pela API.

### PointInfo

```json
{
  "id": "0b7b2f6e-7a49-4a5f-9c1e-2d3a4b5c6d7e",
  "position": "Aorta",
  "animalId": "9f2c1d3e-4b5a-6c7d-8e9f-0a1b2c3d4e5f"
}
```

### ScenarioInfo

```json
{
  "id": "6a1d2e3f-4b5c-6d7e-8f90-1a2b3c4d5e6f",
  "name": "Sopro mitral",
  "description": "Gravação de sopro mitral",
  "audioPath": "sopro_mitral_123.wav",
  "pointId": "0b7b2f6e-7a49-4a5f-9c1e-2d3a4b5c6d7e"
}
```

### Paginação

As rotas de listagem exigem os parâmetros de query `page` e `size`:

- `page`: número da página, começando em `0`.
- `size`: quantidade de itens por página.

Exemplo:

```http
GET /points/me?page=0&size=20
```

## 6. Dados do dispositivo

### 6.1 Consultar o próprio dispositivo

```http
GET /devices/me
```

Resposta `200 OK`: um `DeviceInfo`.

Erros possíveis:

- `401`: token ausente, inválido ou expirado.
- `403`: token sem `ROLE_DEVICE`.

## 7. Rotas de pontos de ausculta

### 7.1 Listar os pontos do animal associado

```http
GET /points/me?page=0&size=20
```

Resposta `200 OK`: lista de `PointInfo`.

Se o dispositivo não tiver animal associado, a resposta será `200 OK` com lista vazia (`[]`).

Erros possíveis:

- `400`: `page` ou `size` ausente/inválido.
- `401`: token ausente, inválido ou expirado.
- `403`: token sem `ROLE_DEVICE`.

### 7.2 Buscar um ponto por ID

```http
GET /points/{id}
```

Resposta `200 OK`: um `PointInfo`.

Erros possíveis:

- `401`: token inválido/expirado.
- `403`: o ponto pertence a outro animal ou o dispositivo não tem animal associado.
- `404`: ponto não encontrado.

### 7.3 Criar um ponto para o animal associado

```http
POST /points
Content-Type: application/json
```

Corpo:

```json
{
  "position": "Aorta"
}
```

> Para o dispositivo, o `animalId` é ignorado. O servidor sempre usa o animal associado ao
> dispositivo autenticado.

Resposta `201 Created`, sem corpo.

Erros possíveis:

- `400`: JSON malformado ou dados inválidos.
- `401`: token inválido/expirado.
- `403`: token sem permissão de device ou dispositivo sem animal associado.

### 7.4 Atualizar a posição de um ponto

```http
PATCH /points/{id}
Content-Type: application/json
```

Corpo:

```json
{
  "position": "Pulmonar"
}
```

Resposta `200 OK`, sem corpo.

Erros possíveis:

- `400`: JSON malformado ou dados inválidos.
- `401`: token inválido/expirado.
- `403`: o ponto pertence a outro animal ou o dispositivo não tem animal associado.
- `404`: ponto não encontrado.

### 7.5 Remover um ponto

```http
DELETE /points/{id}
```

Resposta `200 OK`, sem corpo.

Erros possíveis:

- `401`: token inválido/expirado.
- `403`: o ponto pertence a outro animal ou o dispositivo não tem animal associado.
- `404`: ponto não encontrado.

## 8. Rotas de cenários/áudio

### 8.1 Listar os cenários do animal associado

```http
GET /api/v1/scenarios/me?page=0&size=20
```

Resposta `200 OK`: lista de `ScenarioInfo`.

Se o dispositivo não tiver animal associado, a resposta será `200 OK` com lista vazia (`[]`).

Erros possíveis:

- `400`: `page` ou `size` ausente/inválido.
- `401`: token inválido/expirado.
- `403`: token sem `ROLE_DEVICE`.

### 8.2 Listar os cenários de um ponto

```http
GET /api/v1/scenarios?id={pointId}&page=0&size=20
```

Resposta `200 OK`: lista de `ScenarioInfo`.

O ponto precisa pertencer ao animal associado ao dispositivo.

Erros possíveis:

- `400`: parâmetros ausentes/inválidos.
- `401`: token inválido/expirado.
- `403`: o ponto pertence a outro animal ou o dispositivo não tem animal associado.
- `404`: ponto não encontrado.

### 8.3 Buscar cenários do animal associado por nome

```http
GET /api/v1/scenarios/search?name=sopro&page=0&size=20
```

Resposta `200 OK`: lista de `ScenarioInfo` filtrada pelo nome e pelo animal associado ao
dispositivo. Se o dispositivo não tiver animal associado, a lista será vazia.

Erros possíveis:

- `400`: parâmetros ausentes/inválidos.
- `401`: token inválido/expirado.

### 8.4 Buscar um cenário por ID

```http
GET /api/v1/scenarios/{id}
```

Resposta `200 OK`: um `ScenarioInfo`.

O cenário precisa pertencer a um ponto do animal associado ao dispositivo.

Erros possíveis:

- `401`: token inválido/expirado.
- `403`: o cenário pertence a outro animal ou o dispositivo não tem animal associado.
- `404`: cenário não encontrado.

> O dispositivo **não** pode criar, atualizar, excluir ou reenviar áudio de cenários.
> Essas operações são exclusivas do ADMIN.

## 9. Erros comuns

| Status | Quando ocorre |
|---|---|
| `400` | Parâmetro obrigatório ausente, parâmetro inválido ou JSON malformado |
| `401` | Token ausente, inválido, expirado ou com assinatura inválida |
| `403` | Token sem `ROLE_DEVICE`, ponto/cenário de outro animal ou dispositivo sem animal associado |
| `404` | Ponto, cenário ou dispositivo não encontrado |
| `500` | Erro inesperado no servidor |

Os erros de autorização e recursos normalmente retornam JSON no formato:

```json
{
  "message": "Acesso negado"
}
```

O `401` de token de dispositivo inválido pode ser retornado como erro padrão do servlet, sem corpo
JSON.

## 10. Fluxo recomendado

1. O dispositivo se autocadastra via MQTT em `register`, enviando `mac`, `public_key` e
   `signature`; o cadastro direto no banco permanece disponível como alternativa administrativa.
2. O administrador pode associar o dispositivo a um animal pela dashboard ou diretamente no banco.
3. O dispositivo gera um JWT ES256 com `iss`, `mac` e `exp`.
4. O dispositivo pode chamar `GET /devices/me` para confirmar o vínculo com o animal.
5. O dispositivo chama `GET /points/me` para obter seus pontos.
6. Para cada ponto, o dispositivo pode chamar:
   - `GET /api/v1/scenarios?id={pointId}&page=0&size=20`, ou
   - `GET /api/v1/scenarios/me` para obter todos os cenários de uma vez.
7. O dispositivo correlaciona o áudio ao ponto usando `pointId`.
8. O campo `audioPath` indica o caminho/nome do arquivo de áudio salvo pelo servidor.
9. Quando necessário, o dispositivo pode atualizar ou remover apenas os pontos do animal associado.

## 11. Observações

- Não existem endpoints HTTP de criação, atualização ou exclusão de dispositivo.
- O autocadastro é feito pelo tópico MQTT `register`; a resposta é publicada em `register/ack/{mac}`.
- As rotas administrativas de leitura de dispositivos não fazem parte do contrato do firmware.
- `GET /points?animalId=...` não é permitido para `DEVICE` (retorna `403`). Use `GET /points/me`.
- O dispositivo não recebe a chave pública em nenhuma resposta.
- O `mac` é comparado de forma exata após `trim`.
- O token é de curta duração: não há refresh token nem revogação no servidor.
- Toda leitura de cenário é limitada ao animal associado ao dispositivo.
- As listagens são ordenadas pelo servidor: pontos por `position` e cenários por `name`.
- A associação com o animal é opcional; sem animal, as listagens de pontos/cenários retornam vazias
  e as operações de escrita em pontos são negadas com `403`.
