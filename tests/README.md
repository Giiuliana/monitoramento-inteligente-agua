# Testes de desempenho

## Objetivo

Avaliar o tempo de resposta do sensor HX711 e do atuador
buzzer durante a simulação do protótipo no Wokwi.

## Resultados

| Medição | Buzzer (ms) | HX711 até MQTTX (ms) |
|---------|-------------|---------------------|
| 1       | 1,887       | 833,33              |
| 2       | 1,084       | 800,00              |
| 3       | 1,085       | 400,00              |
| 4       | 1,727       | 466,67              |
| Média   | 1,446       | 625,00              |

## Metodologia

- Buzzer: análise dos sinais digitais por meio do
  analisador lógico do Wokwi e dos arquivos VCD.
- HX711: análise quadro a quadro das gravações,
  considerando o intervalo entre a detecção do evento
  no Wokwi e sua exibição no MQTTX Web.

## Limitações

Os resultados foram obtidos em ambiente simulado.
As medições do HX711 incluem a comunicação MQTT
e a atualização visual das interfaces, não representando
isoladamente o tempo de resposta do sensor.
