/*
 * Boiler Assistant 3.3.11 - read-only diagnostics dashboard fragment.
 * License: CC BY-NC-SA 4.0
 */
R"diagnostics(
<script>
const diagnosticSection=document.createElement('div');
diagnosticSection.id='diagnostics';
diagnosticSection.innerHTML=`<div class="card"><h2>Diagnostic Test</h2>
<p>ZC signal: <strong id="fanZc">Unavailable</strong></p>
<p>Driver: <strong id="fanDriver">Unavailable</strong></p>
<p class="hint">Read-only software status, not measured fan speed or proof of AC power.
Valid timing: 60 Hz only. Auto Tank permits PWM fallback; Continuous waits for stable ZC.
Missing pulses may mean power off or a wiring/module fault.</p>
<pre id="diagnosticCodes" style="white-space:pre-wrap;overflow-wrap:anywhere">Live data unavailable</pre>
<p class="hint">Current conditions only, not fault history. INFO may be normal.
No code does not prove hardware is healthy. Reports omit credentials.</p>
<button id="copyDiagnostics">Copy Report</button><button id="saveDiagnostics">Save Report</button>
<p id="diagnosticCopyStatus" role="status"></p>
<textarea id="diagnosticReport" readonly aria-label="Diagnostic report" style="width:100%;min-height:180px"></textarea></div>`;
document.querySelector('main').insertBefore(diagnosticSection,document.querySelector('main nav'));
function diagnosticCodes(s){
 if(!s)return ['DATA01 [UNKNOWN] Live controller data unavailable'];
 const rows=[],f=s.fan_diag;
 if(Number.isInteger(f)&&f>=0&&f<=255){
  if(!(f&1))rows.push('FAN01 [ACTIVE] Phase timer or ZC IRQ unavailable');
  if(f&16)rows.push('ZC02 [ACTIVE] Noise suppression; retrying');
  else if(!(f&2))rows.push('ZC01 [INFO] No recent pulses; power off or signal fault');
  else if(!(f&4))rows.push('ZC02 [ACTIVE] Invalid pulse timing');
  else if(!(f&8))rows.push('ZC03 [INFO] Valid pulses; stabilizing');
  else rows.push('ZC00 [OK] Stable zero-cross signal');
  if(!(f&128)&&(f&64)&&(!(f&32)||!(f&4)))rows.push('FAN02 [INFO] Unsynchronized fallback');
 }else rows.push('ZC04 [UNKNOWN] Driver diagnostics unavailable');
 if(s.safety_state===1)rows.push('SAFE01 [LATCHED] High-temperature lockout');
 if(s.safety_state===2)rows.push('SAFE02 [LATCHED] Sensor lockout');
 if(s.alert?.includes('EMBER GUARDIAN'))rows.push('SAFE03 [LATCHED] Ember Guardian');
 if(s.exhaust_sensor_ok===false||s.exhaust_fallback)rows.push('TEMP01 [ACTIVE] Exhaust reading fault / fallback');
 if(s.tank_sensor_ok===false)rows.push('TEMP02 [INFO] Control tank reading unavailable');
 (s.water||[]).forEach(p=>{if(p.sensor_ok===false)rows.push('TEMP03 [ACTIVE] Water probe '+p.index+' stale')});
 return rows;
}
function renderFanDiagnostics(s){
 const f=s?.fan_diag,available=Number.isInteger(f)&&f>=0&&f<=255;
 text('fanZc',!available?'Unavailable':f&16?'NOISY - retrying':f&8?'VALID / STABLE':f&4?'VALID - stabilizing':f&2?'Pulses present / invalid timing':'NO RECENT PULSES');
 text('fanDriver',!available?'Unavailable':!(f&1)?(f&128?'OFF':'PWM fallback available')+' / timer or ZC IRQ unavailable':f&128&&!(f&8)?'WAIT / OUTPUT INHIBITED':!(f&64)?'OFF / no output command':f&32&&f&4?'ZC PHASE CONTROL':'PWM / UNSYNCHRONIZED FALLBACK');
 text('diagnosticCodes',diagnosticCodes(s).join('\n'));
}
function diagnosticReport(s){
 const fields=['fan_diag','burn_state_text','safety_text','fan_demand','fan_final','exhaust_raw','exhaust_smooth','tank_temp','exhaust_sensor_ok','tank_sensor_ok','wifi_ok','rssi','loop_last_ms','loop_max_ms','network_last_ms','network_max_ms'];
 return ['Boiler Assistant 3.3.11 diagnostics',new Date().toISOString(),'Software observations only; not fan-speed or mains-wiring verification.',...diagnosticCodes(s),...(s?fields.filter(k=>Object.prototype.hasOwnProperty.call(s,k)).map(k=>k+': '+s[k]):[])].join('\n');
}
function prepareDiagnosticReport(){
 const report=document.getElementById('diagnosticReport');
 report.value=diagnosticReport(Date.now()-lastStateMs<=10000?lastState:null);
 report.value+='\n'+healthLines(lastStateMs&&Date.now()-lastStateMs<=10000?lastState:null).join('\n');
 return report;
}
document.getElementById('copyDiagnostics').onclick=async()=>{
 const report=prepareDiagnosticReport();
 try{
  if(!navigator.clipboard?.writeText)throw new Error('Clipboard unavailable on this connection');
  await navigator.clipboard.writeText(report.value);
  text('diagnosticCopyStatus','Report copied');
 }catch(e){report.focus();report.select();text('diagnosticCopyStatus','Automatic copy failed. Copy the selected report manually.')}
};
function saveDiagnosticReport(){
 const report=prepareDiagnosticReport(),link=document.createElement('a');
 let url;
 try{
  url=URL.createObjectURL(new Blob([report.value],{type:'text/plain;charset=utf-8'}));
  link.href=url;link.download='boiler-diagnostics-'+new Date().toISOString().replace(/[:.]/g,'-')+'.txt';
  document.body.appendChild(link);link.click();
  text('diagnosticCopyStatus','Download requested. Check Downloads or your browser save/share menu.');
 }catch(e){report.focus();report.select();text('diagnosticCopyStatus','Download failed. Copy the selected report manually.')}
 finally{link.remove();if(url)setTimeout(()=>URL.revokeObjectURL(url),60000)}
}
document.getElementById('saveDiagnostics').onclick=saveDiagnosticReport;
setInterval(()=>{if(Date.now()-lastStateMs>10000)renderFanDiagnostics(null)},1000);
</script>
)diagnostics"
