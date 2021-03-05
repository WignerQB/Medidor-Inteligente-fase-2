/*
  *Versao 2 do braço 1
  *O medidor ler a EEPROM para ver quantas fazes lerá
   
   Pinagem do módulo SD na ESP32
   CS: D5
   SCK:D18
   MOSI:D23
   MISO:D19
*/

#include <Wire.h>
#include <DS3231.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "EmonLib.h"
#include <driver/adc.h>
#include "time.h"
#include <SPI.h>
#include <EEPROM.h>

#define EEPROM_SIZE 1
#define SD_CS 5
#define ADC_BITS    10
#define ADC_COUNTS  (1<<ADC_BITS)
//Fase 1
#define pinSensorI_f1 34
#define pinSensorV_f1 33 
#define vCalibration_f1 600
#define currCalibration_f1 4.7
#define phase_shift_f1 1.7
//Fase 2 
#define pinSensorI_f2 35
#define pinSensorV_f2 25
#define vCalibration_f2 600
#define currCalibration_f2 4.7
#define phase_shift_f2 1.7
//Fase 3 
#define pinSensorI_f3 32
#define pinSensorV_f3 26
#define vCalibration_f3 600
#define currCalibration_f3 4.7
#define phase_shift_f3 1.7

//Declaracao de variáveis String --------------------------------------------
String RTCdata, Dados_de_medicao, Dados_SD, LabelSD, Mensagem_Print;

//Declaracao de variáveis de bibliotecas-------------------------------------
RTClib myRTC;
EnergyMonitor emon_f1, emon_f2, emon_f3;

//Declaracao de variáveis byte-----------------------------------------------
byte Year;
byte Month;
byte Date;
byte DoW;
byte Hour;
byte Minute;
byte Second;

//Declaracao de variáveis unsigned long----------------------------------------
unsigned long timerDelay1 = 5000, timerDelay2 = 10000;
unsigned long lastTime1 = 0, lastTime2 = 0;

//Declaracao de variáveis float-------------------------------------------------
float kWh_FP = 0, kWh_I = 0, kWh_P = 0;
float TensaoAlimentacao_f1 = 0, FatorPotencia_f1 = 0, PotenciaAparente_f1 = 0, PotenciaReal_f1 = 0;
float TensaoAlimentacao_f2 = 0, FatorPotencia_f2 = 0, PotenciaAparente_f2 = 0, PotenciaReal_f2 = 0;
float TensaoAlimentacao_f3 = 0, FatorPotencia_f3 = 0, PotenciaAparente_f3 = 0, PotenciaReal_f3 = 0;
//float ConsumoEsperado = 10.00, MetaDiaria, ConsumoDiario = 0, ConsumoTotal = 0, ValorDokWh;//Todos valores simulados
/*
 * kWh_FP: kWh referente ao consumido no horário Fora de Ponta
 * kWh_I: kWh referente ao consumido no horário Intermediário
 * kWh_P: kWh referente ao consumido no horário Ponta
*/

//Declaracao de variáveis double------------------------------------------------
double Irms_f1 = 0, Irms_f2 = 0, Irms_f3 = 0;

//Declaracao de variáveis int---------------------------------------------------
int ContadorDeDias = 30, DiaAtual, flag_setup = 0;
int blue = 33, green = 32, LedDeErro = 2, ErroNoPlanejamento = 0;
/*
   green -> indica se conseguiu enviar para web
   blue -> indica se conseguiu gravar no SD card
*/
int Vetor_Leitura_kWh[500], Agrupar_kWh = 0;
int i = 0 , j, caseTR = 0, NumFases;

enum ENUM {
  f_medicao,
  incrementar,
  printar
};

ENUM estado = f_medicao, estado_antigo;

//Funçoes ----------------------------------------------------------------------

void appendFile(fs::FS &fs, const char * path, String message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, String message) {
  Serial.printf("Writing file: %s\n", path);

  /* cria uma variável "file" do tipo "File", então chama a função
    open do parâmetro fs recebido. Para abrir, a função open recebe
    os parâmetros "path" e o modo em que o arquivo deve ser aberto
    (nesse caso, em modo de escrita com FILE_WRITE)
  */
  File file = fs.open(path, FILE_WRITE);
  //verifica se foi possivel criar o arquivo
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  /*grava o parâmetro "message" no arquivo. Como a função print
    tem um retorno, ela foi executada dentro de uma condicional para
    saber se houve erro durante o processo.*/
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
}

void readFile(fs::FS &fs){
  //Lendo o arquivo kWh_FP.txt--------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n", "/kWh_FP.txt");
    
  File file_kWh_FP = fs.open("/kWh_FP.txt");
  if(!file_kWh_FP){
    Serial.println("Failed to open file for reading");
    return;
  }
        
  Serial.print("Read from file: ");
  while(file_kWh_FP.available()){
    Vetor_Leitura_kWh[i] = file_kWh_FP.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar_kWh = Agrupar_kWh + (Vetor_Leitura_kWh[j] - 48) * pow(10, i - j - 1);
  }
  kWh_FP = float(Agrupar_kWh) / 1000000;
  i = 0;
  file_kWh_FP.close();
  //----------------------------------------------------------------------------------------------------
  //Lendo o arquivo kWh_FP.txt--------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n", "/kWh_I.txt");
    
  File file_kWh_I = fs.open("/kWh_I.txt");
  if(!file_kWh_I){
    Serial.println("Failed to open file for reading");
    return;
  }
        
  Serial.print("Read from file: ");
  while(file_kWh_I.available()){
    Vetor_Leitura_kWh[i] = file_kWh_I.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar_kWh = Agrupar_kWh + (Vetor_Leitura_kWh[j] - 48) * pow(10, i - j - 1);
  }
  kWh_I = float(Agrupar_kWh) / 1000000;
  i = 0;
  file_kWh_I.close();
  //----------------------------------------------------------------------------------------------------
  //Lendo o arquivo kWh_FP.txt--------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n", "/kWh_P.txt");
    
  File file_kWh_P = fs.open("/kWh_P.txt");
  if(!file_kWh_P){
    Serial.println("Failed to open file for reading");
    return;
  }
        
  Serial.print("Read from file: ");
  while(file_kWh_P.available()){
    Vetor_Leitura_kWh[i] = file_kWh_P.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar_kWh = Agrupar_kWh + (Vetor_Leitura_kWh[j] - 48) * pow(10, i - j - 1);
  }
  kWh_P = float(Agrupar_kWh) / 1000000;
  i = 0;
  file_kWh_P.close();
  //----------------------------------------------------------------------------------------------------
}

void SD_config() {
  SD.begin(SD_CS);
  if (!SD.begin()) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);

  // Create a file on the SD card and write labels
  File file = SD.open("/ICDataLogger.csv");
  if (!file) {
    LabelSD = "Data \tHorário \tTensao Alimentacao_f1 \tIrms_f1 \tPotencia Real_f1 \tPotencia Aparente_f1 \tFator de Potencia_f1 \t" ;
    LabelSD = LabelSD + "Tensao Alimentacao_f2 \tIrms_f2 \tPotencia Real_f2 \tPotencia Aparente_f2 \tFator de Potencia_f2 \t" ;
    LabelSD = LabelSD + "Tensao Alimentacao_f3 \tIrms_f3 \tPotencia Real_f3 \tPotencia Aparente_f3 \tFator de Potencia_f3 \tkWh Horário Fora de Ponta \tkWh Horário Intermediário \tHorário de Ponta\n" ;
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/ICDataLogger.csv", LabelSD);
  }
  else {
    Serial.println("File already exists");
  }
  file.close();


  // Create a file on the SD card and write kWh_FP
  File file_kWh_FP = SD.open("/kWh_FP.txt");
  if (!file_kWh_FP) {
    Serial.println("kWh_FP doens't exist");
    Serial.println("Creating file kWh_FP...");
    writeFile(SD, "/kWh_FP.txt", "0");
  }
  else {
    Serial.println("File already exists");
  }
  file_kWh_FP.close();

  // Create a file on the SD card and write kWh_I
  File file_kWh_I = SD.open("/kWh_I.txt");
  if (!file_kWh_I) {
    Serial.println("kWh_I doens't exist");
    Serial.println("Creating file kWh_I...");
    writeFile(SD, "/kWh_I.txt", "0");
  }
  else {
    Serial.println("File already exists");
  }
  file_kWh_I.close();

  // Create a file on the SD card and write kWh_P
  File file_kWh_P = SD.open("/kWh_P.txt");
  if (!file_kWh_P) {
    Serial.println("kWh_P doens't exist");
    Serial.println("Creating file kWh_P...");
    writeFile(SD, "/kWh_P.txt", "0");
  }
  else {
    Serial.println("File already exists");
  }
  file_kWh_P.close();

  readFile(SD);
}

void setup () {
  Serial.begin(57600);
  Wire.begin();
  EEPROM.begin(EEPROM_SIZE);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  analogReadResolution(10);
  Serial.println("\n\nDigite o número de fases(1,2 ou 3) que o medidor inteligente terá de ler!");
  NumFases = EEPROM.read(0);
  Serial.println("O medidor lerá " + String(NumFases) + " fases");

  emon_f1.voltage(pinSensorV_f1, vCalibration_f1, phase_shift_f1); // Voltage: input pin f1, calibration f1, phase_shift_f1
  emon_f1.current(pinSensorI_f1, currCalibration_f1); // Current: input pin f1, calibration f1.
  
  if(NumFases == 2){
    emon_f2.voltage(pinSensorV_f2, vCalibration_f2, phase_shift_f2); // Voltage: input pin f2, calibration f2, phase_shift_f2
    emon_f2.current(pinSensorI_f2, currCalibration_f2); // Current: input pin f2, calibration f2.
  }
  else{
    emon_f2.voltage(pinSensorV_f2, vCalibration_f2, phase_shift_f2); // Voltage: input pin f2, calibration f2, phase_shift_f2
    emon_f2.current(pinSensorI_f2, currCalibration_f2); // Current: input pin f2, calibration f2.
  
    emon_f3.voltage(pinSensorV_f3, vCalibration_f3, phase_shift_f3); // Voltage: input pin f3, calibration f3, phase_shift_f3
    emon_f3.current(pinSensorI_f3, currCalibration_f3); // Current: input pin f3, calibration f3.
  }
  
  SD_config();
}

void loop () {
  DateTime now = myRTC.now();
  switch (estado) {//Funcionalidades
    case f_medicao://Estado aonde é feita a mensuração da corrente, tensão, as potências ativas e reativas e o fator de potência
      //Nesse estado também é realizado a verificação de possíveis erros dado a imprecisão do sensor de corrente.
      //Serial.println("Estado f_medicao");
      
      emon_f1.calcVI(60, 900);
      if(NumFases == 2 || NumFases == 3){
        emon_f2.calcVI(60, 900);
      }
      if(NumFases == 3){
        emon_f3.calcVI(60, 900);
      }
      
      //Fase 1
      TensaoAlimentacao_f1   = emon_f1.Vrms;
      Irms_f1 = emon_f1.calcIrms(1480);  // Calculate Irms_f1 only
      PotenciaReal_f1 = emon_f1.realPower;
      PotenciaAparente_f1 = emon_f1.apparentPower;
      FatorPotencia_f1 = emon_f1.powerFactor;
      
      if(NumFases == 2 || NumFases == 3){
        //Fase 2
        TensaoAlimentacao_f2   = emon_f2.Vrms;
        Irms_f2 = emon_f2.calcIrms(1480);  // Calculate Irms_f2 only
        PotenciaReal_f2 = emon_f2.realPower;
        PotenciaAparente_f2 = emon_f2.apparentPower;
        FatorPotencia_f2 = emon_f2.powerFactor;
      }
      if(NumFases == 3){
        //Fase 3
        TensaoAlimentacao_f3   = emon_f3.Vrms;
        Irms_f3 = emon_f3.calcIrms(1480);  // Calculate Irms_f3 only
        PotenciaReal_f3 = emon_f3.realPower;
        PotenciaAparente_f3 = emon_f3.apparentPower;
        FatorPotencia_f3 = emon_f3.powerFactor;
      }
      
      
      if (PotenciaReal_f1 >= 0){
        estado = incrementar;
      }
      else {
        Serial.println("\n!!Atençao!! Inverta o sentido do sensor de corrente!!\n");
        //        digitalWrite(LedDeErro, HIGH);
        //        digitalWrite(green, LOW);
        //        digitalWrite(blue, LOW);
      }
      break;

    case incrementar://Nesse estado é feito o cálculo do consumo em R$ e em kWh e feito o backup dos dados
      //Serial.println("\n\n\n" + String(millis() - lastTime2) + "\n\n\n");
      if ((millis() - lastTime2) >= timerDelay2) {//Horário Intermediário
        if((int(now.hour()) == 18)||(int(now.hour()) == 22)){
          kWh_I = kWh_I + ((PotenciaReal_f1 + PotenciaReal_f2 + PotenciaReal_f3) / 360) / 1000;
          Dados_de_medicao = String(TensaoAlimentacao_f1) + "\t"  + String(Irms_f1) + "\t"  + String(PotenciaReal_f1) + "\t"  + String(PotenciaAparente_f1) + "\t"  + String(FatorPotencia_f1) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f2) + "\t"  + String(Irms_f2) + "\t"  + String(PotenciaReal_f2) + "\t"  + String(PotenciaAparente_f2) + "\t"  + String(FatorPotencia_f2) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f3) + "\t"  + String(Irms_f3) + "\t"  + String(PotenciaReal_f3) + "\t"  + String(PotenciaAparente_f3) + "\t"  + String(FatorPotencia_f3) + "\t" + String(kWh_FP* 1000000) + "\t" + String(kWh_I* 1000000) + "\t" + String(kWh_P* 1000000);
          Dados_SD = RTCdata + "\t" + Dados_de_medicao + "\n";
          appendFile(SD, "/ICDataLogger.csv", Dados_SD);
          writeFile(SD, "/kWh_I.txt", String((int)(kWh_I*1000000)));
        }
        else if((int(now.hour()) >= 19)&&(int(now.hour()) <= 21)){//Horário de Ponta
          kWh_P = kWh_P + ((PotenciaReal_f1 + PotenciaReal_f2 + PotenciaReal_f3) / 360) / 1000;  
          Dados_de_medicao = String(TensaoAlimentacao_f1) + "\t"  + String(Irms_f1) + "\t"  + String(PotenciaReal_f1) + "\t"  + String(PotenciaAparente_f1) + "\t"  + String(FatorPotencia_f1) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f2) + "\t"  + String(Irms_f2) + "\t"  + String(PotenciaReal_f2) + "\t"  + String(PotenciaAparente_f2) + "\t"  + String(FatorPotencia_f2) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f3) + "\t"  + String(Irms_f3) + "\t"  + String(PotenciaReal_f3) + "\t"  + String(PotenciaAparente_f3) + "\t"  + String(FatorPotencia_f3) + "\t" + String(kWh_FP* 1000000) + "\t" + String(kWh_I* 1000000) + "\t" + String(kWh_P* 1000000);
          Dados_SD = RTCdata + "\t" + Dados_de_medicao + "\n";
          appendFile(SD, "/ICDataLogger.csv", Dados_SD);
          writeFile(SD, "/kWh_P.txt", String((int)(kWh_P*1000000)));
        }
        else{
          kWh_FP = kWh_FP + ((PotenciaReal_f1 + PotenciaReal_f2 + PotenciaReal_f3) / 360) / 1000;  
          Dados_de_medicao = String(TensaoAlimentacao_f1) + "\t"  + String(Irms_f1) + "\t"  + String(PotenciaReal_f1) + "\t"  + String(PotenciaAparente_f1) + "\t"  + String(FatorPotencia_f1) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f2) + "\t"  + String(Irms_f2) + "\t"  + String(PotenciaReal_f2) + "\t"  + String(PotenciaAparente_f2) + "\t"  + String(FatorPotencia_f2) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f3) + "\t"  + String(Irms_f3) + "\t"  + String(PotenciaReal_f3) + "\t"  + String(PotenciaAparente_f3) + "\t"  + String(FatorPotencia_f3) + "\t" + String(kWh_FP* 1000000) + "\t" + String(kWh_I* 1000000) + "\t" + String(kWh_P* 1000000);
          Dados_SD = RTCdata + "\t" + Dados_de_medicao + "\n";
          appendFile(SD, "/ICDataLogger.csv", Dados_SD);
          writeFile(SD, "/kWh_FP.txt", String((int)(kWh_FP*1000000)));
        }
        lastTime2 = millis();
      }
      estado = printar;
      break;

    case printar://Printa as informações lidas e calculadas
      if ((millis() - lastTime1) >= timerDelay1)
      {
        RTCdata = String(now.year()) + "/" + String(now.month()) + "/" + String(now.day()) + "\t" + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());
        Serial.println(RTCdata);
        //Fase1
        Serial.print(" Vrms1: " + String(TensaoAlimentacao_f1) + " V     Irms_f1: " + String(Irms_f1) + " A     Potência Real1: " + String(PotenciaReal_f1));
        Serial.println(" W     Potência Aparente1: " + String(PotenciaAparente_f1) + " VA    Fator de Potência1: " + String(FatorPotencia_f1));
        //Fase2
        Serial.print(" Vrms2: " + String(TensaoAlimentacao_f2) + " V     Irms_f2: " + String(Irms_f2) + " A     Potência Real2: " + String(PotenciaReal_f2));
        Serial.println(" W     Potência Aparente2: " + String(PotenciaAparente_f2) + " VA    Fator de Potência2: " + String(FatorPotencia_f2));
        //Fase3
        Serial.print(" Vrms3: " + String(TensaoAlimentacao_f3) + " V     Irms_f3: " + String(Irms_f3) + " A     Potência Real3: " + String(PotenciaReal_f3)); 
        Serial.println(" W     Potência Aparente3: " + String(PotenciaAparente_f3) + " VA   Fator de Potência3: " + String(FatorPotencia_f3));
        Serial.print("kWh*1000000: ");
        if((int(now.hour()) == 18)||(int(now.hour()) == 22)){
          Serial.println(String(kWh_I * 1000000));
        }
        else if((int(now.hour()) >= 19)&&(int(now.hour()) <= 21)){//Horário de Ponta
          Serial.println(String(kWh_P * 1000000));
        }
        else{
          Serial.println(String(kWh_FP * 1000000));
        }
        estado = f_medicao;
        lastTime1 = millis();
      }
      break;

    default :
      //Serial.println("Estado default");
      break;
  }
}
