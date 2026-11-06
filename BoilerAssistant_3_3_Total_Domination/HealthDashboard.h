/*
 * Read-only health checks; adjacent dashboard literal. CC BY-NC-SA 4.0
 */
R"health(
<script>
const sensorSection=document.getElementById('sensors');
const sensorReadings=document.createElement('div');sensorReadings.id='sensorReadings';
while(sensorSection.firstChild)sensorReadings.appendChild(sensorSection.firstChild);
const sensorSubtabs=document.createElement('div');sensorSubtabs.className='actions';
sensorSubtabs.innerHTML='<button id="showSensorReadings" aria-pressed="true">Sensor Readings</button><button id="showDiagnosticTest" aria-pressed="false">Diagnostic Test</button>';
const diagnosticPanel=document.getElementById('diagnostics');diagnosticPanel.hidden=true;
sensorSection.append(sensorSubtabs,sensorReadings,diagnosticPanel);
function showSensorTest(test){
sensorReadings.hidden=test;diagnosticPanel.hidden=!test;
document.getElementById('showSensorReadings').setAttribute('aria-pressed',String(!test));
document.getElementById('showDiagnosticTest').setAttribute('aria-pressed',String(test));
if(lastState)renderProbes(lastState.water);
pollNow();
}
document.getElementById('showSensorReadings').onclick=()=>showSensorTest(false);
document.getElementById('showDiagnosticTest').onclick=()=>showSensorTest(true);
let healthSd=null,healthSdAt=0;
function healthLines(s,includeNames=false){
if(!s)return ['DATA01 [UNKNOWN] No fresh controller response'];
const value=v=>Number.isFinite(v)?String(v):'unavailable';
const age=v=>Number.isFinite(v)?(v/1000).toFixed(1)+'s':'never / unknown';
const status=(ok,fresh)=>ok===false?'FAULT / unavailable':ok===true&&fresh?'READING OK':'UNKNOWN / stale';
const lines=[
'Exhaust: '+status(s.exhaust_sensor_ok,[s.exhaust_raw,s.exhaust_smooth,s.exhaust_age_ms].every(Number.isFinite)&&s.exhaust_age_ms<=1000)+'; raw '+value(s.exhaust_raw)+' F; filtered '+value(s.exhaust_smooth)+' F; last good '+age(s.exhaust_age_ms),
'Outdoor BME280: '+status(s.env?.sensor_ok,[s.env?.temp_f,s.env?.humidity,s.env?.pressure].every(Number.isFinite))+'; '+value(s.env?.temp_f)+' F; humidity '+value(s.env?.humidity)+'%; pressure '+value(s.env?.pressure)+' hPa (age not tracked)'
];
const water=Array.isArray(s.water)?s.water:[];
lines.push('Discovered water probes: '+water.length+' / 8. Undiscovered probes cannot be identified from this list.');
water.forEach(p=>lines.push('Water #'+p.index+(includeNames?' '+(p.name||'Unnamed'):'')+(p.controls_tank?' [CONTROL TANK]':' [MONITOR]')+': '+status(p.sensor_ok,Number.isFinite(p.temp_f)&&Number.isFinite(p.age_ms)&&p.age_ms<=3000)+'; '+value(p.temp_f)+' F; last good '+age(p.age_ms)));
lines.push('Wi-Fi: '+(s.wifi_ok===true?'CONNECTED':s.wifi_ok===false?'DISCONNECTED':'UNKNOWN')+'; RSSI '+value(s.rssi)+' dBm',
'Loop last / max: '+value(s.loop_last_ms)+' / '+value(s.loop_max_ms)+' ms; network '+value(s.network_last_ms)+' / '+value(s.network_max_ms)+' ms',
'SD logger: '+(typeof refreshSdLog!=='function'?'not included in this version':healthSd&&Date.now()-healthSdAt<=10000?healthSd:'not checked recently'));
return lines;
}
const healthCard=document.createElement('div');
healthCard.innerHTML='<button id="runHealthCheck">Run Diagnostic Test</button><p id="healthResult" role="status">Not run</p><p class="hint">One read-only check of ZC, driver, safety, sensors and system status. Checks available readings, not calibration or physical operation. No fan/damper movement, probe rescan or SD write test. LCD, keypad, actual fan speed and mains wiring are NOT TESTED.</p><pre id="healthReadings" style="white-space:pre-wrap;overflow-wrap:anywhere"></pre>';
const reportButton=document.getElementById('copyDiagnostics');
reportButton.parentNode.insertBefore(healthCard,reportButton);
function renderHealth(){
text('healthReadings',healthLines(lastStateMs&&Date.now()-lastStateMs<=10000?lastState:null,true).join('\n'));
}
document.getElementById('runHealthCheck').onclick=async()=>{
const button=document.getElementById('runHealthCheck');button.disabled=true;
text('healthResult','Checking current controller status...');
healthSd=null;healthSdAt=0;
try{
await state();
let sdError='';
if(typeof refreshSdLog==='function'){
try{
const sd=await requestJson('/api/sd');
if(sd.error)throw new Error(sd.error);
healthSd='state '+(sd.state||'unknown')+'; rows lost '+(sd.rows_lost??'unknown')+'; last error '+(sd.last_error||'none reported');
healthSdAt=Date.now();
}catch(e){healthSd='UNAVAILABLE: SD status request failed';healthSdAt=Date.now();sdError=' SD status request failed: '+e.message;}
}
renderHealth();
prepareDiagnosticReport();
text('healthResult','Status check finished '+new Date().toLocaleTimeString()+'. Review codes and readings; this is not an all-hardware PASS.'+sdError);
}catch(e){lastStateMs=0;renderHealth();prepareDiagnosticReport();text('healthResult','Check failed: '+e.message)}
finally{button.disabled=false}
};
setInterval(renderHealth,1000);
</script>
)health"
