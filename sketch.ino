
#include "HX711.h"
#include <EspMQTTClient.h>
#include <math.h>

// PINOS
#define PIN_DT 16
#define PIN_SCK 4
#define PIN_BOTAO 14
#define PIN_BUZZER 25
#define PIN_COMANDO 26

// PARAMETROS DA GARRAFA
const float FATOR_CALIBRACAO = 420.0;
const float PESO_GARRAFA_VAZIA = 200.0;
const float CAPACIDADE_AGUA = 1000.0;
const float LIMITE_ALERTA = 100.0;

// ESTABILIDADE
const float LIMIAR_ESTABILIDADE_G = 15.0;
const float LIMIAR_VARIACAO_G = 20.0;
const int LEITURAS_ESTAVEIS_NECESSARIAS = 3;

// WI-FI E MQTT
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;

const char* MQTT_CLIENT_ID = "ESP32-Agua-Wokwi-7319";

const char* MQTT_TOPIC =
  "mackenzie/monitoramento-agua/prototipo-7319/telemetria";

// CLIENTE MQTT
EspMQTTClient mqttClient(
  WIFI_SSID,
  WIFI_PASSWORD,
  MQTT_HOST,
  MQTT_CLIENT_ID,
  MQTT_PORT
);

// HX711
HX711 balanca;

// CONTROLE DA MEDICAO
float aguaReferenciaAnterior = 0;
float aguaConsumidaAcumulada = 0;

float massaAtualGramas = 0;
float aguaRestanteAtual = 0;

bool medicaoIniciada = false;
bool buzzerAtivo = false;

// BOTAO
int estadoAnteriorBotao = HIGH;
unsigned long ultimoAcionamento = 0;
unsigned long ultimaLeitura = 0;

// ESTABILIDADE
float massaCandidata = 0;
int leiturasEstaveis = 0;

// MQTT
unsigned long ultimaPublicacaoMQTT = 0;

// CONTADOR DE TESTES DO BUZZER
unsigned int numeroAcionamentos = 0;

// PUBLICAR TELEMETRIA
bool publicarTelemetria(const char* evento) {

  if (!mqttClient.isConnected()) {
    Serial.println("MQTT desconectado. Publicacao nao enviada.");
    return false;
  }

  char payload[256];

  snprintf(
    payload,
    sizeof(payload),
    "{\"evento\":\"%s\","
    "\"massa_atual_g\":%.1f,"
    "\"agua_consumida_acumulada_ml\":%.0f,"
    "\"agua_restante_ml\":%.0f,"
    "\"buzzer_ativo\":%s}",
    evento,
    massaAtualGramas,
    aguaConsumidaAcumulada,
    aguaRestanteAtual,
    buzzerAtivo ? "true" : "false"
  );

  bool enviado = mqttClient.publish(
    MQTT_TOPIC,
    String(payload)
  );

  if (enviado) {
    Serial.print("Publicacao MQTT enviada: ");
    Serial.println(evento);
  } else {
    Serial.println("Falha na publicacao MQTT.");
  }

  return enviado;
}

// CALLBACK DE CONEXAO
void onConnectionEstablished() {

  Serial.println("=========================");
  Serial.println("MQTT conectado ao EMQX!");
  Serial.println("=========================");

  if (medicaoIniciada) {
    publicarTelemetria("reconexao");
  }
}

void setup() {

  Serial.begin(115200);

  pinMode(PIN_BOTAO, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_COMANDO, OUTPUT);

  // Iniciar com buzzer e marcador desligados
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_COMANDO, LOW);

  // INICIALIZAR HX711
  balanca.begin(PIN_DT, PIN_SCK);

  Serial.println("Monitoramento Inteligente de Agua");
  Serial.println("Inicializando HX711...");

  if (!balanca.wait_ready_timeout(5000)) {
    Serial.println("Erro: HX711 nao encontrado!");

    while (true) {
      delay(1000);
    }
  }

  balanca.tare(20);

  Serial.println("Tara concluida!");
  Serial.println("Coloque a garrafa cheia e pressione o botao.");

  // CONFIGURAR MQTT
  mqttClient.enableDebuggingMessages();
  mqttClient.setKeepAlive(30);

  Serial.println("Inicializando Wi-Fi e MQTT...");
}

void loop() {

  // MANTER MQTT ATIVO
  mqttClient.loop();

  if (!balanca.is_ready()) {
    return;
  }

  int estadoBotao = digitalRead(PIN_BOTAO);

  // INICIAR MEDICAO
  if (estadoAnteriorBotao == HIGH &&
      estadoBotao == LOW &&
      millis() - ultimoAcionamento > 300) {

    long leitura = balanca.read_average(10);

    float massaInicial =
      (leitura - balanca.get_offset()) *
      1000.0 / FATOR_CALIBRACAO;

    if (massaInicial < 0) {
      massaInicial = 0;
    }

    aguaReferenciaAnterior = constrain(
      massaInicial - PESO_GARRAFA_VAZIA,
      0.0,
      CAPACIDADE_AGUA
    );

    aguaConsumidaAcumulada = 0;

    massaAtualGramas = massaInicial;
    aguaRestanteAtual = aguaReferenciaAnterior;

    medicaoIniciada = true;

    // Reiniciar buzzer e marcador
    buzzerAtivo = false;
    noTone(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_COMANDO, LOW);

    leiturasEstaveis = 0;
    massaCandidata = massaInicial;

    ultimoAcionamento = millis();
    ultimaPublicacaoMQTT = millis();

    Serial.println("Nova medicao iniciada!");

    Serial.print("Massa inicial: ");
    Serial.print(massaInicial, 1);
    Serial.println(" g");

    publicarTelemetria("nova_medicao");
  }

  estadoAnteriorBotao = estadoBotao;

  // LEITURA A CADA SEGUNDO
  if (millis() - ultimaLeitura >= 1000) {

    ultimaLeitura = millis();

    long leituraBruta = balanca.read_average(10);

    float massaAtual =
      (leituraBruta - balanca.get_offset()) *
      1000.0 / FATOR_CALIBRACAO;

    if (massaAtual < 0) {
      massaAtual = 0;
    }

    Serial.println("-------------------------");

    Serial.print("Massa atual: ");
    Serial.print(massaAtual, 1);
    Serial.println(" g");

    if (!medicaoIniciada) {
      Serial.println("Pressione o botao para iniciar.");
      return;
    }

    // VERIFICAR ESTABILIDADE
    if (leiturasEstaveis == 0 ||
        fabs(massaAtual - massaCandidata) >
        LIMIAR_ESTABILIDADE_G) {

      massaCandidata = massaAtual;
      leiturasEstaveis = 1;

    } else {

      massaCandidata =
        (massaCandidata * leiturasEstaveis + massaAtual) /
        (leiturasEstaveis + 1);

      if (leiturasEstaveis <
          LEITURAS_ESTAVEIS_NECESSARIAS) {

        leiturasEstaveis++;
      }
    }

    if (leiturasEstaveis <
        LEITURAS_ESTAVEIS_NECESSARIAS) {

      Serial.println("Aguardando estabilizacao da massa...");
      return;
    }

    massaAtualGramas = massaCandidata;

    // CALCULAR AGUA RESTANTE
    aguaRestanteAtual = constrain(
      massaAtualGramas - PESO_GARRAFA_VAZIA,
      0.0,
      CAPACIDADE_AGUA
    );

    float variacaoAgua =
      aguaRestanteAtual - aguaReferenciaAnterior;

    bool reabastecimentoDetectado = false;
    bool consumoDetectado = false;

    // IDENTIFICAR CONSUMO
    if (variacaoAgua <= -LIMIAR_VARIACAO_G) {

      aguaConsumidaAcumulada += -variacaoAgua;

      aguaReferenciaAnterior = aguaRestanteAtual;

      consumoDetectado = true;

      Serial.println("Consumo de agua identificado.");

    } else if (variacaoAgua >= LIMIAR_VARIACAO_G) {

      // IDENTIFICAR REABASTECIMENTO
      aguaReferenciaAnterior = aguaRestanteAtual;

      reabastecimentoDetectado = true;

      Serial.println("Reabastecimento identificado!");
      Serial.println("Consumo acumulado mantido.");
    }

    // EXIBIR RESULTADOS
    Serial.print("Agua consumida acumulada: ");
    Serial.print(aguaConsumidaAcumulada, 0);
    Serial.println(" mL");

    Serial.print("Agua restante: ");
    Serial.print(aguaRestanteAtual, 0);
    Serial.println(" mL");

    // PUBLICAR EVENTOS
    if (consumoDetectado) {
      publicarTelemetria("consumo");
    }

    if (reabastecimentoDetectado) {
      publicarTelemetria("reabastecimento");
    }

    // CONTROLE DO BUZZER E ANALISADOR LOGICO
    if (aguaRestanteAtual <= LIMITE_ALERTA &&
        !buzzerAtivo) {

      // D0: instante do comando
      digitalWrite(PIN_COMANDO, HIGH);

      // D1: sinal eletrico do buzzer
      tone(PIN_BUZZER, 2500);

      buzzerAtivo = true;

      numeroAcionamentos++;

      Serial.println("ALERTA: AGUA ACABANDO!");

      Serial.print("Acionamento do buzzer numero: ");
      Serial.println(numeroAcionamentos);

      publicarTelemetria("alerta_agua_baixa");

    } else if (aguaRestanteAtual > LIMITE_ALERTA &&
               buzzerAtivo) {

      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, LOW);

      // Reiniciar marcador para o proximo teste
      digitalWrite(PIN_COMANDO, LOW);

      buzzerAtivo = false;

      Serial.println("Nivel de agua normalizado.");

      publicarTelemetria("nivel_normalizado");
    }

    // PUBLICAR TELEMETRIA A CADA 5 SEGUNDOS
    // Durante leituras estaveis
    if (millis() - ultimaPublicacaoMQTT >= 5000) {

      ultimaPublicacaoMQTT = millis();

      publicarTelemetria("telemetria");
    }
  }
}