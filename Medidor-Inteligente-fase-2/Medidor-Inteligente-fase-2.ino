/*
 * Autor: José Wigner Quintino Bindacco
  *Versao 5 do braço 1
  *É calculado o quanto o consumidor consome em cada faixa de horário do seu dia.
  *É salvo esses valores no arquivo .csv
  *Algumas outras mudanças na estrutura do código que fazem mais sentido para seu funcionamento.

   -O que o medidor consegue fazer até este ponto?
   § Possui leitura polifásica conforme a especificacao do usuário.
   § Salva os dados (consumo em kWh é salvo em arquivos .txt diferentes e salva um apanhado geral para efeito de estudo e análise em um 
   arquivo .csv) em um cartao SD.
   § Salva na EEPROM a quantidade de fases a ser lida, bem como o dia atual para conferir se houve uma mudança de dias.
   § Faz o cálculo da Tarifa Branca para o usuário avaliar se compensa mudar para a modalidade tarifaria. 
   
   -Informaçoes importantes sobre o código para seu funcionamento:
   Dados Salvos na EEPROM:
   Canal 0: Números de fases a ser lido
   Canal 1: Dia Atual
   
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

#define EEPROM_SIZE 4
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
String RTCdata, Dados_de_medicao, Dados_SD, LabelSD, Mensagem_Print, PorcentagemConsumo;

//Declaracao de variáveis de bibliotecas-------------------------------------
RTClib myRTC;
EnergyMonitor emon_f1, emon_f2, emon_f3;

//Declaracao de variáveis unsigned long----------------------------------------
unsigned long timerDelay1 = 5000, timerDelay2 = 10000;
unsigned long lastTime1 = 0, lastTime2 = 0;

//Declaracao de variáveis float-------------------------------------------------
float kWh_FP = 0, kWh_I = 0, kWh_P = 0, kWh_Total;
float TensaoAlimentacao_f1 = 0, FatorPotencia_f1 = 0, PotenciaAparente_f1 = 0, PotenciaReal_f1 = 0;//Fase 1
float TensaoAlimentacao_f2 = 0, FatorPotencia_f2 = 0, PotenciaAparente_f2 = 0, PotenciaReal_f2 = 0;//Fase 2
float TensaoAlimentacao_f3 = 0, FatorPotencia_f3 = 0, PotenciaAparente_f3 = 0, PotenciaReal_f3 = 0;//Fase 3
float MetaMensal = 10.00, MetaDiaria, Consumo = 0, ConsumoDiario, ConsumoMensal = 0, Valor_kWhC = 0.26292;//Variáveis para o controle das metas
float Valor_kWhFP, Valor_kWhP, Valor_kWhI, kz = 0.47;//Variáveis para a implementaçao do cálculo da Tarifa Branca
float ConsumoFP = 0, ConsumoI = 0, ConsumoP = 0, ConsumoMensalTB = 0, FatordeRecompenca = 0.9;
float PerConsumoFP = 0, PerConsumoI = 0, PerConsumoP = 0;
 
/*
 * kWh_FP: kWh referente ao consumido no horário Fora de Ponta
 * kWh_I: kWh referente ao consumido no horário Intermediário
 * kWh_P: kWh referente ao consumido no horário Ponta
*/

//Declaracao de variáveis double------------------------------------------------
double Irms_f1 = 0, Irms_f2 = 0, Irms_f3 = 0;

//Declaracao de variáveis int---------------------------------------------------
int DiaAtual, DiasRestantes, DiaFimMedicao = 30;
//int blue = 33, green = 32, LedDeErro = 2, ErroNoPlanejamento = 0;
/*
   green -> indica se conseguiu enviar para web
   blue -> indica se conseguiu gravar no SD card
*/
int Vetor_Leitura[500], Agrupar = 0;
int i = 0 , j, NumFases;

enum ENUM {
  f_medicao,
  incrementar,
  printar
};

ENUM estado = f_medicao;

//Funçoes ----------------------------------------------------------------------
void CalcTarifaBranca(int NumMes){
  ConsumoMensal = (kWh_FP + kWh_I + kWh_P) * Valor_kWhC;
  ConsumoFP = kWh_FP * Valor_kWhFP;
  ConsumoI = kWh_I * Valor_kWhI;
  ConsumoP = kWh_P * Valor_kWhP;
  ConsumoMensalTB = ConsumoFP + ConsumoI + ConsumoP;
  Serial.print("\nSeu consumo no mês " + String(NumMes) + ", segundo os parâmetros calculados da Tarifa Branca foi de 1000000*R$");
  Serial.print(String(1000000*ConsumoMensalTB) + ". Já seu consumo pela Tarifa convencional foi de 1000000*R$" + String(1000000*ConsumoMensal));
  if(ConsumoMensalTB < FatordeRecompenca*ConsumoMensal){
    Serial.println("\nBaseado no seu consumo deste mês, a adesão a Tarifa Branca traz benefícios na redução dos custos de energia elétrica.");
  }
  else{
    Serial.println("\nBaseado no seu consumo deste mês, a adesão a Tarifa Branca não traz benefícios na redução dos custos de energia elétrica.");
  }
}

void Gerenciamento(int Dia){
  if(Dia != DiaAtual){//Verifica se houve uma mudança de dia, indicando que o dia que estava sendo monitorado finalizou e irá iniciar um 
                      //novo dia de monitoramento
    ConsumoDiario = Consumo - ConsumoMensal;
    ConsumoMensal = Consumo;
    if(ConsumoDiario > MetaDiaria){//Verifica se o usuário cumpriu a meta diária e caso nao tenha cumprido vê a possibilidade de o usuário
                                   //ainda ficar dentro do consumo esperado para o fim do mês.
      Serial.println("\n\n\nAviso: Você consumiu além da meta diária estabelecida!\n\n\n");
      if(ConsumoMensal < MetaMensal){
        MetaDiaria = (MetaMensal - ConsumoMensal)/DiasRestantes;//Calcula a nova meta diária com base no que o cara já consumiu e nos 
        Serial.println("Nova meta diaria calculada: R$" + String(MetaDiaria));//dias que ainda faltam
      }                                                          
    }
    //Serial.println("\n\n\nEstou salvando: " + String(DiaAtual) + "\n\n\n");
    DiaAtual = Dia;
    DiasRestantes = DiaFimMedicao - DiaAtual;
    EEPROM.write(1, DiaAtual);
    writeFile(SD, "/ConsumoMensal.txt", String((int)(1000000*ConsumoMensal)));
  }
}

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
        
  Serial.print("Read from file: kWh_FP.txt");
  while(file_kWh_FP.available()){
    Vetor_Leitura[i] = file_kWh_FP.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar = Agrupar + (Vetor_Leitura[j] - 48) * pow(10, i - j - 1);
  }
  kWh_FP = float(Agrupar) / 1000000;
  i = 0;
  file_kWh_FP.close();
  //----------------------------------------------------------------------------------------------------
  //Lendo o arquivo kWh_I.txt---------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n", "/kWh_I.txt");
    
  File file_kWh_I = fs.open("/kWh_I.txt");
  if(!file_kWh_I){
    Serial.println("Failed to open file for reading");
    return;
  }
        
  Serial.print("Read from file: kWh_I.txt");
  while(file_kWh_I.available()){
    Vetor_Leitura[i] = file_kWh_I.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar = Agrupar + (Vetor_Leitura[j] - 48) * pow(10, i - j - 1);
  }
  kWh_I = float(Agrupar) / 1000000;
  i = 0;
  file_kWh_I.close();
  //----------------------------------------------------------------------------------------------------
  //Lendo o arquivo kWh_P.txt---------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n", "/kWh_P.txt");
    
  File file_kWh_P = fs.open("/kWh_P.txt");
  if(!file_kWh_P){
    Serial.println("Failed to open file for reading");
    return;
  }
        
  Serial.print("Read from file: kWh_P.txt");
  while(file_kWh_P.available()){
    Vetor_Leitura[i] = file_kWh_P.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar = Agrupar + (Vetor_Leitura[j] - 48) * pow(10, i - j - 1);
  }
  kWh_P = float(Agrupar) / 1000000;
  i = 0;
  file_kWh_P.close();
  //----------------------------------------------------------------------------------------------------
  //Lendo o arquivo MetaDiaria.txt----------------------------------------------------------------------
  Serial.printf("Reading file: %s\n", "/MetaDiaria.txt");
    
  File file_MetaDiaria = fs.open("/MetaDiaria.txt");
  if(!file_MetaDiaria){
    Serial.println("Failed to open file for reading");
    return;
  }
        
  Serial.print("Read from file: MetaDiaria.txt");
  while(file_MetaDiaria.available()){
    Vetor_Leitura[i] = file_MetaDiaria.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar = Agrupar + (Vetor_Leitura[j] - 48) * pow(10, i - j - 1);
  }
  MetaDiaria = float(Agrupar) / 100;
  i = 0;
  file_MetaDiaria.close();
  //----------------------------------------------------------------------------------------------------
  //Lendo o arquivo ConsumoMensal.txt----------------------------------------------------------------------
  Serial.printf("Reading file: %s\n", "/ConsumoMensal.txt");
    
  File fileConsumoMensal = fs.open("/ConsumoMensal.txt");
  if(!fileConsumoMensal){
    Serial.println("Failed to open file for reading");
    return;
  }
        
  Serial.print("Read from file: ConsumoMensal.txt");
  while(fileConsumoMensal.available()){
    Vetor_Leitura[i] = fileConsumoMensal.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++){
    Agrupar = Agrupar + (Vetor_Leitura[j] - 48) * pow(10, i - j - 1);
  }
  ConsumoMensal = float(Agrupar) / 1000000;
  i = 0;
  fileConsumoMensal.close();
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
    LabelSD = LabelSD + "Tensao Alimentacao_f3 \tIrms_f3 \tPotencia Real_f3 \tPotencia Aparente_f3 \tFator de Potencia_f3 \tkWh Horário Fora de Ponta \tkWh Horário Intermediário \tHorário de Ponta \t" ;
    LabelSD = LabelSD + "Porcentagem de consumo no horário Fora de Ponta \tPorcentagem de consumo no horário Intermediário \tPorcentagem de consumo no horário de Ponta \n";
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

  // Create a file on the SD card and write MetaDiaria
  File file_MetaDiaria = SD.open("/MetaDiaria.txt");
  if (!file_MetaDiaria) {
    Serial.println("MetaDiaria doens't exist");
    Serial.println("Creating file MetaDiaria...");
    //Calcular a meta diaria de consumo
    MetaDiaria = MetaMensal/31;
    writeFile(SD, "/MetaDiaria.txt", String((int)MetaDiaria*100));
  }
  else {
    Serial.println("File MetaDiaria already exists");
  }
  file_MetaDiaria.close();

  // Create a file on the SD card and write ConsumoMensal
  File fileConsumoMensal = SD.open("/ConsumoMensal.txt");
  if (!fileConsumoMensal) {
    Serial.println("ConsumoMensal.txt doens't exist");
    Serial.println("Creating file ConsumoMensal.txt...");
    writeFile(SD, "/ConsumoMensal.txt", " ");
  }
  else {
    Serial.println("File ConsumoMensal.txt already exists");
  }
  fileConsumoMensal.close();

  readFile(SD);
}

void setup () {
  Serial.begin(57600);
  Wire.begin();
  EEPROM.begin(EEPROM_SIZE);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  analogReadResolution(10);
  NumFases = EEPROM.read(0);
  Serial.println("\nO medidor lerá " + String(NumFases) + " fases");
  DateTime now = myRTC.now();
  if(EEPROM.read(1)>=1 && EEPROM.read(1)<=31){
    DiaAtual = EEPROM.read(1);
  }
  else{
    DiaAtual = now.day();
    EEPROM.write(1, DiaAtual);
  }
  Serial.println("Dia Atual é " + String(DiaAtual));
  DiasRestantes = DiaFimMedicao - DiaAtual;
  
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

  Valor_kWhFP = kz * Valor_kWhC;
  Valor_kWhI = Valor_kWhFP * 3;
  Valor_kWhP = Valor_kWhFP * 5;
  Serial.println("\n\nValor convencional do kWh " + String(Valor_kWhC) +  "\nValor kWh FP " + String(Valor_kWhFP) + "\nValor kWh I " + String(Valor_kWhI) + "\nValor kWh P " + String(Valor_kWhP));
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
      RTCdata = String(now.year()) + "/" + String(now.month()) + "/" + String(now.day()) + "\t" + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second());
      if ((millis() - lastTime2) >= timerDelay2) {
        if((int(now.hour()) == 18)||(int(now.hour()) == 22)){//Horário Intermediário
          kWh_I = kWh_I + ((PotenciaReal_f1 + PotenciaReal_f2 + PotenciaReal_f3) / 360) / 1000;
          Dados_de_medicao = String(TensaoAlimentacao_f1) + "\t"  + String(Irms_f1) + "\t"  + String(PotenciaReal_f1) + "\t"  + String(PotenciaAparente_f1) + "\t"  + String(FatorPotencia_f1) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f2) + "\t"  + String(Irms_f2) + "\t"  + String(PotenciaReal_f2) + "\t"  + String(PotenciaAparente_f2) + "\t"  + String(FatorPotencia_f2) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f3) + "\t"  + String(Irms_f3) + "\t"  + String(PotenciaReal_f3) + "\t"  + String(PotenciaAparente_f3) + "\t"  + String(FatorPotencia_f3) + "\t" + String(kWh_FP* 1000000) + "\t" + String(kWh_I* 1000000) + "\t" + String(kWh_P* 1000000);
          Dados_SD = RTCdata + "\t" + Dados_de_medicao + "\n";
          writeFile(SD, "/kWh_I.txt", String((int)(kWh_I*1000000)));
        }
        else if((int(now.hour()) >= 19)&&(int(now.hour()) <= 21)){//Horário de Ponta
          kWh_P = kWh_P + ((PotenciaReal_f1 + PotenciaReal_f2 + PotenciaReal_f3) / 360) / 1000;  
          Dados_de_medicao = String(TensaoAlimentacao_f1) + "\t"  + String(Irms_f1) + "\t"  + String(PotenciaReal_f1) + "\t"  + String(PotenciaAparente_f1) + "\t"  + String(FatorPotencia_f1) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f2) + "\t"  + String(Irms_f2) + "\t"  + String(PotenciaReal_f2) + "\t"  + String(PotenciaAparente_f2) + "\t"  + String(FatorPotencia_f2) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f3) + "\t"  + String(Irms_f3) + "\t"  + String(PotenciaReal_f3) + "\t"  + String(PotenciaAparente_f3) + "\t"  + String(FatorPotencia_f3) + "\t" + String(kWh_FP* 1000000) + "\t" + String(kWh_I* 1000000) + "\t" + String(kWh_P* 1000000);
          Dados_SD = RTCdata + "\t" + Dados_de_medicao + "\n";
          writeFile(SD, "/kWh_P.txt", String((int)(kWh_P*1000000)));
        }
        else{//Horário Fora de Ponta
          kWh_FP = kWh_FP + ((PotenciaReal_f1 + PotenciaReal_f2 + PotenciaReal_f3) / 360) / 1000;  
          Dados_de_medicao = String(TensaoAlimentacao_f1) + "\t"  + String(Irms_f1) + "\t"  + String(PotenciaReal_f1) + "\t"  + String(PotenciaAparente_f1) + "\t"  + String(FatorPotencia_f1) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f2) + "\t"  + String(Irms_f2) + "\t"  + String(PotenciaReal_f2) + "\t"  + String(PotenciaAparente_f2) + "\t"  + String(FatorPotencia_f2) + "\t";
          Dados_de_medicao = Dados_de_medicao + String(TensaoAlimentacao_f3) + "\t"  + String(Irms_f3) + "\t"  + String(PotenciaReal_f3) + "\t"  + String(PotenciaAparente_f3) + "\t"  + String(FatorPotencia_f3) + "\t" + String(kWh_FP* 1000000) + "\t" + String(kWh_I* 1000000) + "\t" + String(kWh_P* 1000000);
          Dados_SD = RTCdata + "\t" + Dados_de_medicao + "\n";
          writeFile(SD, "/kWh_FP.txt", String((int)(kWh_FP*1000000)));
        }
        lastTime2 = millis();
      }
      //Contabilizaçao de gastos
      kWh_Total = kWh_FP + kWh_I + kWh_P; 
      PerConsumoFP = kWh_FP/kWh_Total;
      PerConsumoI = kWh_I/kWh_Total;
      PerConsumoP = kWh_P/kWh_Total;

      PorcentagemConsumo = String((int)(100*PerConsumoFP)) + "\t" + String((int)(100*PerConsumoI)) + "\t" + String((int)(100*PerConsumoP));
      Dados_SD = RTCdata + "\t" + Dados_de_medicao + "\t" + PorcentagemConsumo + "\n";
      appendFile(SD, "/ICDataLogger.csv", Dados_SD);
      
      Consumo = kWh_Total * Valor_kWhC;
      Gerenciamento(now.day());
//      if((now.day() == 1)&&(now.hour() == 7)&&(now.minute() <= 1)){
//        CalcTarifaBranca(now.month())
//      }
      if((now.day() == 16)&&(now.hour() == 15)&&(now.minute() >= 20)){
        CalcTarifaBranca(now.month());
      }
      estado = printar;
      break;

    case printar://Printa as informações lidas e calculadas
      if ((millis() - lastTime1) >= timerDelay1)
      {
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
        lastTime1 = millis();
      }
      estado = f_medicao; //Reverter(voltar para dentro do escopo)
      break;

    default :
      //Serial.println("Estado default");
      break;
  }
}
