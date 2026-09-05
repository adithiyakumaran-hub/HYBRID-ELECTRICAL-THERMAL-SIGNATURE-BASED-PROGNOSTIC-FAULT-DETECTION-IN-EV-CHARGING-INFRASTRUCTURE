#include <math.h>
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "Sarathi vivo";
const char* password = "sarathi1";
WebServer server(80);

float web_HI, web_I, web_temp, web_life, web_Rc;
String web_fault = "NORMAL";
String web_state = "GREEN";

float web_SR, web_SP, web_Tstress;
float web_degradation;
float web_trend;
unsigned long uptime = 0;

#define PIN_VSUP   34
#define PIN_VLOAD  35
#define PIN_ACS    32
#define PIN_TEMP   33

#define LED_GREEN  23
#define LED_BLUE   22
#define LED_RED    21
#define BUZZER     19
#define LOAD_SW    18

float adc_ref = 3.3;
int   adc_res = 4095;
float divider_ratio = 3.2;

float acs_offset = 2.22;
float acs_sens   = 0.185;

float Rc_ref = 20;
float Pc_ref = 8;
float T_ref  = 10;

float wR = 0.45;
float wP = 0.30;
float wT = 0.25;

float Rc_avg = 0;
float alpha  = 0.5;
int   samples = 30;

float HI_avg     = 0;
float rise_alpha = 0.7;
float decay      = 0.98;
float HI_prev    = 0;

float HI_buffer[5];
int   hi_index   = 0;

int  state       = 0;
bool faultLatch  = false;
unsigned long safeTimer    = 0;
unsigned long startIgnore  = 2000;

unsigned long stateTimer   = 0;
int           prevState    = 0;
unsigned long minStateTime = 2000;

float degradation = 0;

float I_base  = 0;
float I_alpha = 0.02;

float readADC(int pin)
{
  long s = 0;
  for (int i = 0; i < samples; i++)
  {
    s += analogRead(pin);
    delayMicroseconds(150);
  }
  return (float)s / samples;
}

String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Smart Connector Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
body{background:#000000;color:#e0e0e0;margin:0;font-family:Arial,Helvetica,sans-serif;}
header{background:#050810;padding:14px 20px;box-shadow:0 2px 8px rgba(0,0,0,0.8);text-align:center;}
h1{margin:0;font-size:28px;color:#ffffff;letter-spacing:0.04em;}
.container{padding:10px 30px;max-width:1500px;margin:0 auto;}
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-bottom:12px;}
.card{background:#101010;padding:10px 12px;border-radius:10px;box-shadow:0 2px 6px rgba(0,0,0,0.8);text-align:center;}
.label{font-size:12px;color:#9fa8da;text-transform:uppercase;letter-spacing:0.05em;}
.value{font-size:18px;font-weight:bold;margin-top:4px;}
.state-pill{display:inline-block;padding:4px 12px;border-radius:999px;font-size:12px;font-weight:bold;margin-top:6px;}
.state-green{background:#1b5e20;color:#a5d6a7;}
.state-blue{background:#0d47a1;color:#90caf9;}
.state-red{background:#b71c1c;color:#ffab91;}
.fault-text{font-size:14px;margin-top:6px;}
.bar-bg{width:100%;height:8px;border-radius:999px;background:#263238;margin-top:6px;overflow:hidden;}
.bar-fill{height:100%;width:0%;background:linear-gradient(90deg,#00c853,#ffd600,#d50000);transition:width 0.3s ease-out;}
#charts{margin-top:10px;display:grid;grid-template-columns:1fr;gap:8px;}
.chart-card{background:#101010;border-radius:10px;padding:6px 10px;box-shadow:0 2px 6px rgba(0,0,0,0.8);}
.chart-title{font-size:13px;color:#9fa8da;margin:2px 0 4px 4px;text-transform:uppercase;letter-spacing:0.05em;}
.chart-card canvas{background:#05070e;border-radius:8px;display:block;margin:0 auto;}
</style>
</head>
<body>
<header>
  <h1>SMART CONNECTOR MONITORING SYSTEM</h1>
</header>
<div class="container">
  <div class="grid">
    <div class="card"><div class="label">HEALTH INDEX</div><div class="value"><span id="hi">0.00</span> p.u.</div></div>
    <div class="card"><div class="label">STATE</div><div class="state-pill state-green" id="state-pill">GREEN</div></div>
    <div class="card"><div class="label">FAULT</div><div class="fault-text" id="f">NORMAL</div></div>
    <div class="card"><div class="label">CURRENT</div><div class="value"><span id="i">0.00</span> A</div></div>
    <div class="card"><div class="label">TEMPERATURE</div><div class="value"><span id="t">0.0</span> C</div></div>
    <div class="card"><div class="label">REMAINING LIFE</div><div class="value"><span id="l">0.0</span> %</div></div>
    <div class="card"><div class="label">DEGRADATION</div><div class="value"><span id="deg">0.0</span> %</div><div class="bar-bg"><div class="bar-fill" id="deg-bar"></div></div></div>
    <div class="card"><div class="label">CONTACT RESISTANCE</div><div class="value"><span id="rc">0.00</span> Ohm</div></div>
  </div>

  <div id="charts">
    <div class="chart-card">
      <div class="chart-title">HI (P.U.)</div>
      <canvas id="chart_hi" width="1100" height="120"></canvas>
    </div>
    <div class="chart-card">
      <div class="chart-title">CURRENT (A)</div>
      <canvas id="chart_i" width="1100" height="100"></canvas>
    </div>
    <div class="chart-card">
      <div class="chart-title">TEMPERATURE (C)</div>
      <canvas id="chart_t" width="1100" height="100"></canvas>
    </div>
  </div>
</div>

<script>
const hiEl  = document.getElementById('hi');
const iEl   = document.getElementById('i');
const tEl   = document.getElementById('t');
const lEl   = document.getElementById('l');
const fEl   = document.getElementById('f');
const degEl = document.getElementById('deg');
const rcEl  = document.getElementById('rc');
const degBar = document.getElementById('deg-bar');
const statePill = document.getElementById('state-pill');

let ctxHI = document.getElementById('chart_hi').getContext('2d');
let ctxI  = document.getElementById('chart_i').getContext('2d');
let ctxT  = document.getElementById('chart_t').getContext('2d');

const baseLayout = {padding:{left:8,right:8,top:6,bottom:4}};
const baseX = {ticks:{color:'#9fa8da',maxRotation:0,maxTicksLimit:4},grid:{color:'rgba(255,255,255,0.05)'}};

let chartHI = new Chart(ctxHI,{
  type:'line',
  data:{labels:[],datasets:[{label:'HI (p.u.)',data:[],borderColor:'#00e5ff',backgroundColor:'rgba(0,229,255,0.18)',fill:true,tension:0.25,borderWidth:2,pointRadius:1.5,pointHitRadius:6}]},
  options:{
    responsive:false,
    layout:baseLayout,
    plugins:{legend:{labels:{color:'#e0e0e0'}}},
    scales:{x:baseX,y:{min:0,max:1,ticks:{color:'#9fa8da'},grid:{color:'rgba(255,255,255,0.08)'}}}
  }
});

let chartI = new Chart(ctxI,{
  type:'line',
  data:{labels:[],datasets:[{label:'Current (A)',data:[],borderColor:'#ffeb3b',backgroundColor:'rgba(255,235,59,0.15)',fill:true,tension:0.25,borderWidth:2,pointRadius:1.5,pointHitRadius:6}]},
  options:{
    responsive:false,
    layout:baseLayout,
    plugins:{legend:{labels:{color:'#e0e0e0'}}},
    scales:{x:baseX,y:{ticks:{color:'#9fa8da'},grid:{color:'rgba(255,255,255,0.08)'}}}
  }
});

let chartT = new Chart(ctxT,{
  type:'line',
  data:{labels:[],datasets:[{label:'Temp (C)',data:[],borderColor:'#ff7043',backgroundColor:'rgba(255,112,67,0.15)',fill:true,tension:0.25,borderWidth:2,pointRadius:1.5,pointHitRadius:6}]},
  options:{
    responsive:false,
    layout:baseLayout,
    plugins:{legend:{labels:{color:'#e0e0e0'}}},
    scales:{x:baseX,y:{ticks:{color:'#9fa8da'},grid:{color:'rgba(255,255,255,0.08)'}}}
  }
});

function pushPoint(chart,value){
  const timeLabel = new Date().toLocaleTimeString();
  if(chart.data.labels.length>60){
    chart.data.labels.shift();
    chart.data.datasets[0].data.shift();
  }
  chart.data.labels.push(timeLabel);
  chart.data.datasets[0].data.push(value);
  chart.update('none');
}

function updateStatePill(state){
  statePill.classList.remove('state-green','state-blue','state-red');
  if(state==='GREEN') statePill.classList.add('state-green');
  else if(state==='BLUE') statePill.classList.add('state-blue');
  else statePill.classList.add('state-red');
  statePill.innerText = state;
}

function updateDegradation(deg){
  const c = Math.max(0,Math.min(100,deg));
  degBar.style.width = c.toFixed(0) + '%';
}

setInterval(()=>{
  fetch('/data').then(r=>r.json()).then(d=>{
    hiEl.innerText  = d.HI.toFixed(2);
    iEl.innerText   = d.I.toFixed(2);
    tEl.innerText   = d.temp.toFixed(1);
    lEl.innerText   = d.life.toFixed(1);
    fEl.innerText   = d.fault;
    degEl.innerText = d.deg.toFixed(1);
    rcEl.innerText  = d.Rc.toFixed(2);
    updateStatePill(d.state);
    updateDegradation(d.deg);
    pushPoint(chartHI,d.HI);
    pushPoint(chartI,d.I);
    pushPoint(chartT,d.temp);
  }).catch(e=>console.error(e));
},800);
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", page);
}

void handleData()
{
  String json = "{";
  json += "\"HI\":"   + String(web_HI,   3) + ",";
  json += "\"I\":"    + String(web_I,    3) + ",";
  json += "\"temp\":" + String(web_temp, 2) + ",";
  json += "\"life\":" + String(web_life, 2) + ",";
  json += "\"deg\":"  + String(web_degradation, 2) + ",";
  json += "\"Rc\":"   + String(web_Rc, 2) + ",";
  json += "\"state\":\"" + web_state + "\",";
  json += "\"fault\":\"" + web_fault + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("System Starting...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  analogSetPinAttenuation(PIN_VSUP,  ADC_11db);
  analogSetPinAttenuation(PIN_VLOAD, ADC_11db);
  analogSetPinAttenuation(PIN_ACS,   ADC_11db);
  analogSetPinAttenuation(PIN_TEMP,  ADC_11db);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);
  pinMode(LED_RED,   OUTPUT);
  pinMode(BUZZER,    OUTPUT);
  pinMode(LOAD_SW,   OUTPUT);

  digitalWrite(LOAD_SW, HIGH);

  for (int i = 0; i < 5; i++) HI_buffer[i] = 0;
}

void loop()
{
  server.handleClient();

  uptime = millis() / 1000;

  if (millis() < startIgnore) return;

  float raw_vs = readADC(PIN_VSUP);
  float raw_vl = readADC(PIN_VLOAD);
  float raw_i  = readADC(PIN_ACS);
  float raw_t  = readADC(PIN_TEMP);

  float Vs = (raw_vs * adc_ref / adc_res) * divider_ratio;
  float Vl = (raw_vl * adc_ref / adc_res) * divider_ratio;

  float Vacs = raw_i * adc_ref / adc_res;
  float I = fabs((Vacs - acs_offset) / acs_sens);
  if (I < 0.05) I = 0;

  bool noLoad = (I < 0.05);

  if (HI_avg < 0.2)
    I_base = (1 - I_alpha) * I_base + I_alpha * I;

  float temp = (raw_t * adc_ref / adc_res) * 100;
  float Tstress = (temp - 30) / T_ref;
  if (Tstress < 0) Tstress = 0;
  if (Tstress > 1) Tstress = 1;

  float Rc = 0, Pc = 0;

  if (I > 0.15 && Vs > Vl)
  {
    Rc = (Vs - Vl) / I;
    if (Rc > 0 && Rc < 120)
      Rc_avg = alpha * Rc + (1 - alpha) * Rc_avg;

    Pc = I * I * Rc_avg;
  }

  float SR = Rc_avg / Rc_ref;
  float SP = Pc    / Pc_ref;

  if (I < 0.1) { SR = 0; SP = 0; }

  if (SR > 1) SR = 1;
  if (SP > 1) SP = 1;

  float HI = wR * SR + wP * SP + wT * Tstress;

  float fast = rise_alpha * HI + (1 - rise_alpha) * HI_avg;
  if (fast > HI_avg)
    HI_avg = fast;
  else
    HI_avg = decay * HI_avg;

  HI_buffer[hi_index++] = HI_avg;
  if (hi_index >= 5) hi_index = 0;

  float sum = 0;
  for (int i = 0; i < 5; i++) sum += HI_buffer[i];
  HI_avg = sum / 5.0;

  if (noLoad)
  {
    HI_avg  = 0;
    HI_prev = 0;
    Rc_avg  = 0;
    for (int i = 0; i < 5; i++) HI_buffer[i] = 0;
  }

  float trend = HI_avg - HI_prev;
  HI_prev = HI_avg;

  float life_hours = 120 * exp(-2 * HI_avg);
  float life_pct   = (life_hours / 120.0f) * 100.0f;
  if (life_pct < 0)   life_pct = 0;
  if (life_pct > 100) life_pct = 100;
  float deg_pct    = 100.0f - life_pct;

  degradation = deg_pct;

  if (HI_avg < 0.18) state = 0;
  else if (HI_avg < 0.27) state = 1;
  else state = 2;

  String fault = "NORMAL";

  if (state == 2) fault = "CRITICAL FAILURE";
  else if (trend > 0.03) fault = "ARC FAULT";
  else if (!noLoad && I_base > 0.2 && I > 1.4 * I_base) fault = "OVERLOAD";
  else if (Rc_avg > 60) fault = "CONNECTOR WEAR";
  else if (state == 1) fault = "DEGRADATION";

  if (state == 0)
  {
    digitalWrite(LED_GREEN,HIGH);
    digitalWrite(LED_BLUE,LOW);
    digitalWrite(LED_RED,LOW);
  }
  else if (state == 1)
  {
    digitalWrite(LED_GREEN,LOW);
    digitalWrite(LED_BLUE,HIGH);
    digitalWrite(LED_RED,LOW);
  }
  else
  {
    digitalWrite(LED_GREEN,LOW);
    digitalWrite(LED_BLUE,LOW);
    digitalWrite(LED_RED,HIGH);
  }

  unsigned long now = millis();

  if (state == 0)
  {
    digitalWrite(BUZZER, LOW);
  }
  else if (state == 1)
  {
    bool beep = (now % 500 < 250);
    digitalWrite(BUZZER, beep ? HIGH : LOW);
  }
  else
  {
    digitalWrite(BUZZER, HIGH);
  }

  digitalWrite(LOAD_SW, (state != 2));

  web_HI          = HI_avg;
  web_I           = I;
  web_temp        = temp;
  web_life        = life_pct;
  web_Rc          = Rc_avg;
  web_fault       = fault;
  web_degradation = deg_pct;
  web_SR          = SR;
  web_SP          = SP;
  web_Tstress     = Tstress;
  web_trend       = trend;

  if (state == 0) web_state = "GREEN";
  else if (state == 1) web_state = "BLUE";
  else web_state = "RED";

  Serial.print(HI_avg, 3);
  Serial.print(",");
  Serial.print(degradation, 2);
  Serial.print(",");
  Serial.print(life_hours, 2);
  Serial.print(",");
  Serial.print(Rc_avg, 2);
  Serial.print(",");
  Serial.print(I, 3);
  Serial.print(",");
  Serial.print(Vl, 2);
  Serial.print(",");
  Serial.print(temp, 2);
  Serial.print(",");
  Serial.print(SR, 3);
  Serial.print(",");
  Serial.print(SP, 3);
  Serial.print(",");
  Serial.print(Tstress, 3);
  Serial.print(",");
  Serial.println(fault);

  delay(350);
}
