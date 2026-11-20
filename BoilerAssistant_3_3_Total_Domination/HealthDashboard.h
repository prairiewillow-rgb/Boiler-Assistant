/*
 * Read-only health checks; adjacent dashboard literal. CC BY-NC-SA 4.0
 */
R"health(
<script>
let healthSd=null,healthSdAt=0,healthDiag=null,healthDiagAt=0;
let healthSettings='Saved settings: not captured - run the test';
function reportSettings(data,keys){
 if(!data||typeof data!=='object'||Array.isArray(data))throw new Error('Invalid settings response');
 return Object.fromEntries(keys.split(' ').map(key=>{
  const value=data[key];
  if(['id','supply','return'].includes(key)?typeof value!=='string':typeof value!=='boolean'&&!Number.isFinite(value))throw new Error('Missing setting: '+key);
  return [key,value];
 }));
}
async function captureHealthSettings(){
 healthSettings='Saved operating settings captured '+new Date().toISOString()+' (not unsaved edits; temperatures F, volumes US gallons)\n';
 let errors='';
 const sections=[
  ['/api/settings','General','exhaust_setpoint control_mode deadband adaptive_learning boost_time boost_fan_pct damper_delay clamp_min clamp_max deadzone_fan ember_minutes flue_low flue_recovery tank_low tank_high env_units self_clean_enabled self_clean_interval self_clean_start_hour self_clean_end_hour self_clean_utc_offset self_clean_dst push_enabled share_data'],
  ['/api/seasonal','Seasonal','auto_enabled lockout_hours legacy_mode'],
  ['/api/water_calibration','Water calibration','storage_available']
 ];
 for(const [url,label,keys] of sections){
  try{
   const data=await requestJson(url);
   if(data.error)throw new Error(data.error);
   const settings=reportSettings(data,keys);
   if(label==='Seasonal'){
    if(!Array.isArray(data.seasons)||data.seasons.length!==4)throw new Error('Expected four seasons');
    settings.seasons=data.seasons.map(row=>reportSettings(row,'start_f hysteresis_f exhaust_f tank_low_f tank_high_f fan_max_pct boost_time boost_fan_pct damper_delay'));
   }
   if(label==='Water calibration'){
    if(!Array.isArray(data.probes))throw new Error('Expected calibration probes');
    settings.probes=data.probes.map(row=>reportSettings(row,'id offset_f controls_tank'));
   }
   healthSettings+=label+': '+JSON.stringify(settings)+'\n';
  }catch(e){healthSettings+=label+': UNAVAILABLE: '+e.message+'\n';errors+=' '+label+' settings failed: '+e.message;}
 }
 return errors;
}
function healthFreezeLines(){
 if(!healthDiagAt||Date.now()-healthDiagAt>10000)return ['Restart / freeze clues: not checked recently'];
 const fields=Object.fromEntries(String(healthDiag?.diag||'').split(' ').map(part=>part.split('=')));
 const labels={up:'Uptime (s)',heap:'Free heap bytes (reusable + unallocated)',blk:'Largest free heap block bytes (checked now)',low:'Lowest free heap bytes seen (sampled)',stk:'Peak stack bytes',wd:'Watchdog resets',slow:'Slowest operation',ok:'HTTP requests served',rto:'HTTP receive timeouts',sto:'HTTP send timeouts',wf:'HTTP write failures',hr:'HTTP restarts',lost:'Wi-Fi losses',chip:'Wi-Fi module resets'};
 return ['Last restart: '+(healthDiag?.reset||'unavailable'),...Object.entries(labels).map(([key,label])=>label+': '+(fields[key]??'unavailable')),'At last test: '+(healthDiag?.diag||'unavailable')];
}
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
 const fresh=water.filter(p=>p.sensor_ok===true&&Number.isFinite(p.temp_f)&&Number.isFinite(p.age_ms)&&p.age_ms<=3000).length;
 water.forEach(p=>lines.push('Water #'+p.index+(includeNames?' '+(p.name||'Unnamed'):'')+(p.controls_tank?' [CONTROL TANK]':' [MONITOR]')+': '+status(p.sensor_ok,Number.isFinite(p.temp_f)&&Number.isFinite(p.age_ms)&&p.age_ms<=3000)+'; '+value(p.temp_f)+' F; last good '+age(p.age_ms)));
 lines.push('Water probe freshness: '+fresh+' / '+water.length+' fresh; '+(water.length-fresh)+' stale, faulty or unknown',
  'Control tank: '+status(s.tank_sensor_ok,Number.isFinite(s.tank_temp))+'; '+value(s.tank_temp)+' F');
 lines.push('Wi-Fi: '+(s.wifi_ok===true?'CONNECTED':s.wifi_ok===false?'DISCONNECTED':'UNKNOWN')+'; RSSI '+value(s.rssi)+' dBm',
  'Loop last / max: '+value(s.loop_last_ms)+' / '+value(s.loop_max_ms)+' ms; network '+value(s.network_last_ms)+' / '+value(s.network_max_ms)+' ms',
  'SD logger: '+(typeof refreshSdLog!=='function'?'not included in this version':healthSd&&Date.now()-healthSdAt<=10000?healthSd:'not checked recently'),
  ...healthFreezeLines());
 return lines;
}
const healthCard=document.createElement('div');
healthCard.innerHTML='<h3>Test instructions</h3><ol><li>Tap <strong>Run Diagnostic Test</strong> when the problem happens, before restarting if possible. Wait until the check finishes.</li><li>Tap <strong>Save Report</strong>. Find the text file in Downloads/Files. Attach it to your message or email.</li><li>Or tap <strong>Copy Report</strong> and paste into your message. If copy fails, hold the report box, select all and copy.</li></ol><p class="hint">Report includes saved operating settings, not passwords or unsaved edits. Include the problem and time. Send the report even if the check is incomplete. Nothing is sent automatically. No output movement or settings changes; not a physical hardware test.</p><button id="runHealthCheck">Run Diagnostic Test</button>';
const reportButton=document.getElementById('copyDiagnostics');
reportButton.parentNode.insertBefore(healthCard,reportButton);
let healthSummary='';
function renderHealth(){
 const fresh=lastStateMs&&Date.now()-lastStateMs<=10000;
 if(!fresh)renderFanDiagnostics(null);
}
document.getElementById('runHealthCheck').onclick=async()=>{
 const button=document.getElementById('runHealthCheck');button.disabled=true;
 document.getElementById('diagnosticReport').value='Checking current controller status...';
 healthSd=null;healthSdAt=0;healthDiag=null;healthDiagAt=0;
 healthSettings='Saved settings: not captured - test did not finish';
 try{
  await state();
  let checkError='';
  if(typeof refreshSdLog==='function'){
   try{
    const sd=await requestJson('/api/sd');
    if(sd.error)throw new Error(sd.error);
    healthSd='state '+(sd.state||'unknown')+'; rows lost '+(sd.rows_lost??'unknown')+'; last error '+(sd.last_error||'none reported')+
     '\nSD last runtime drop (this boot): '+(sd.last_drop_error||'none recorded')+
     '\nClock: '+(sd.clock_valid===true?'VALID':sd.clock_valid===false?'UNAVAILABLE - SD charts require valid time':'unknown')+
     '; free '+(sd.free_mb??'unknown')+' MB; pending rows '+(sd.rows_pending??'unknown')+
     '\nSD lost history / events / burns: '+(sd.history_rows_lost??'unknown')+' / '+(sd.events_lost??'unknown')+' / '+(sd.burns_lost??'unknown')+
     '; write last / max '+(sd.last_write_ms??'unknown')+' / '+(sd.max_write_ms??'unknown')+' ms'+
     '\nSD suspended: '+(sd.logging_suspended??'unknown')+'; retry seconds '+(sd.retry_seconds??'unknown')+
     '; SD step last / max '+(sd.last_sd_step_ms??'unknown')+' / '+(sd.max_sd_step_ms??'unknown')+' ms'+
     '; max download read '+(sd.max_download_read_ms??'unknown')+' ms'+
     '\nSD mount attempts / init fails / card init code: '+(sd.mount_attempts??'unknown')+' / '+(sd.mount_init_fails??'unknown')+' / '+(sd.card_init_error??'unknown')+
     '\nSD boot sectors (latest mount): '+(sd.boot_mount_result==null?'not checked or unavailable':
      (sd.boot_sector_zero||'LBA 0 not read')+'; partition: '+(sd.boot_partition||'not read')+'; FatFs '+sd.boot_mount_result);
    healthSdAt=Date.now();
   }catch(e){healthSd='UNAVAILABLE: SD status request failed: '+e.message;healthSdAt=Date.now();checkError+=' SD status request failed: '+e.message;}
  }
  try{const d=await requestJson('/api/diag');if(d.error)throw new Error(d.error);if(typeof d.reset!=='string'||typeof d.diag!=='string')throw new Error('Incomplete diagnostic response');healthDiag=d;healthDiagAt=Date.now()}catch(e){healthDiag={reset:'UNAVAILABLE: '+e.message,diag:'UNAVAILABLE: '+e.message};healthDiagAt=Date.now();checkError+=' Restart / network status request failed: '+e.message;}
  checkError+=await captureHealthSettings();
  if(!lastStateMs||Date.now()-lastStateMs>10000)checkError+=' Live readings expired; run again.';
  renderHealth();
  healthSummary=(checkError?'Check incomplete':'Status check finished')+' '+new Date().toLocaleTimeString()+': not an all-hardware PASS.'+checkError;
  prepareDiagnosticReport();
 }catch(e){lastStateMs=0;renderHealth();healthSummary='Check failed: '+e.message;prepareDiagnosticReport()}
 finally{button.disabled=false}
};
</script>
)health"
