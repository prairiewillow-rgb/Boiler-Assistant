/*
 * Boiler Assistant - Settings / Seasonal.
 * Adjacent string literal included by DashboardHTML.h.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
R"seasonhtml(
<style>
.settingsSubtabs{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin:0 0 16px}
.settingsSubtabs button{min-width:0;min-height:44px;padding:10px 6px}
.settingsSubtabs [aria-selected=true]{background:#163b3e;border-color:var(--cyan);color:var(--cyan)}
#settings [hidden]{display:none!important}
#selfCleanSettings .selfCleanPanel{margin:0 0 14px}
#seasonalPanel .seasonTable{overflow-x:auto}
#seasonalPanel table{width:100%;border-collapse:collapse;min-width:650px}
#seasonalPanel th,#seasonalPanel td{padding:9px;text-align:left;border-bottom:1px solid var(--line)}
#seasonalPanel input[type=number]{min-width:85px;width:100%;padding:9px;border:1px solid var(--line);border-radius:6px;background:#101b21;color:var(--text);font:inherit}
#seasonalPanel fieldset{border:0;padding:0;margin:0;min-width:0}
#seasonalPanel input[type=checkbox]{width:auto}
#seasonalPanel .seasonActive{background:#163b3e}
#seasonalPanel .seasonWarning{color:var(--hot);white-space:pre-line}
#seasonalPanel .seasonSummary{white-space:pre-line;line-height:1.7}
#seasonalPanel .card{margin-bottom:14px}
#seasonTrial{width:100%;margin:8px 0;accent-color:var(--cyan)}
#seasonTransitions{display:block;height:8px;border-radius:4px}
#seasonalPanel summary{cursor:pointer;font-weight:600}
#seasonCards{display:none}
#seasonalPanel .seasonField{display:grid;gap:6px;margin-bottom:12px}
#seasonalPanel .seasonBadge{font-size:.8rem;color:var(--cyan);margin-left:8px}
@media(max-width:680px){
 #seasonalPanel #seasonEditor{display:none}
 #seasonCards{display:block}
 #seasonalPanel input[type=number]{min-height:44px;font-size:16px}
 #seasonalPanel .seasonProfile{padding:14px;border:1px solid var(--line);border-radius:8px;margin-bottom:14px}
 #seasonalPanel .seasonProfile.seasonActive{border-color:var(--cyan)}
}
</style>
<script>
const seasonNames=['Summer','Spring/Fall','Winter','Extreme'];
let seasonMetric=false;
const seasonFields=[
 {key:'start_f',label:'Outdoor changeover (F)',min:-100,max:150},
 {key:'hysteresis_f',label:'Extra degrees before switching (F difference)',min:0,max:50},
 {key:'exhaust_f',label:'Exhaust setpoint (F)',min:200,max:900},
 {key:'tank_low_f',label:'Tank low (F)',min:40,max:188},
 {key:'tank_high_f',label:'Tank high (F)',min:41,max:189},
 {key:'fan_max_pct',label:'Fan maximum (%)',min:0,max:100},
 {key:'damper_delay',label:'Damper delay (s, 0=skip)',min:0,max:300},
 {key:'boost_time',label:'Boost time (s, 0=skip)',min:0,max:600},
 {key:'boost_fan_pct',label:'Boost fan (%)',min:0,max:100}
];
const settingsRoot=document.getElementById('settings');
const generalPanel=document.createElement('div');
generalPanel.id='generalPanel';generalPanel.setAttribute('role','tabpanel');
while(settingsRoot.firstChild)generalPanel.appendChild(settingsRoot.firstChild);
settingsRoot.appendChild(generalPanel);
settingsRoot.appendChild(sdSection);
const notificationSettings=document.getElementById('notificationSettings');
settingsRoot.appendChild(notificationSettings);
const sensorsPanel=document.getElementById('sensors');
sensorsPanel.hidden=true;sensorsPanel.style.display='block';
sensorsPanel.setAttribute('role','tabpanel');settingsRoot.appendChild(sensorsPanel);
const diagnosticsPanel=document.getElementById('diagnostics');diagnosticsPanel.hidden=true;
diagnosticsPanel.setAttribute('role','tabpanel');settingsRoot.appendChild(diagnosticsPanel);
const selfCleanSettings=document.createElement('div');
selfCleanSettings.id='selfCleanSettings';selfCleanSettings.hidden=true;
selfCleanSettings.setAttribute('role','tabpanel');
selfCleanSettings.innerHTML='<h2>Self-clean</h2><p class="hint">Auto Tank only. Save changes before using Run on next burn.</p>';
selfCleanSettings.appendChild(document.getElementById('selfCleanPanel'));
const selfCleanFields=document.createElement('div');selfCleanFields.className='form';
ids.filter(id=>id.startsWith('self_clean_')).forEach(id=>{
 selfCleanFields.appendChild(document.getElementById(id).closest('.field'));
});
const selfCleanCard=document.createElement('div');selfCleanCard.className='card';
selfCleanCard.appendChild(selfCleanFields);selfCleanSettings.appendChild(selfCleanCard);
settingsRoot.appendChild(selfCleanSettings);
const seasonalPanel=document.createElement('div');
seasonalPanel.id='seasonalPanel';seasonalPanel.hidden=true;seasonalPanel.setAttribute('role','tabpanel');
seasonalPanel.innerHTML=`<h2>Seasonal</h2>
<div class="card"><h2>Running now</h2><div id="seasonLive" class="seasonSummary" role="status">Not loaded.</div>
<details><summary>Settings in use</summary><div id="seasonApplied" class="seasonSummary"></div></details></div>
<fieldset id="seasonFields" disabled>
<div class="card" id="seasonInteraction"><h2>When does the season change?</h2>
<div class="field"><label for="seasonPair">Compare two seasons</label>
<select id="seasonPair"><option value="0">Summer / Spring/Fall</option>
<option value="1">Spring/Fall / Winter</option><option value="2">Winter / Extreme</option></select></div>
<label for="seasonTrial">Try an outdoor temperature: <output id="seasonTrialValue" for="seasonTrial"></output></label>
<input id="seasonTrial" type="range" step="1">
<i id="seasonTransitions" aria-hidden="true"></i>
<div id="seasonBands" class="seasonSummary"></div>
<p class="hint">Preview only. Nothing here changes the boiler.</p>
<details><summary>How switching works</summary><p class="hint">The first selection uses the outdoor changeover numbers without the extra degrees. After that, the boiler changes one season at a time, when the wait has ended. Without an outdoor reading it keeps the last season.</p></details>
</div>
<details class="card" id="seasonProfiles"><summary>Edit seasonal settings</summary>
<p><label><input id="seasonAuto" type="checkbox">Switch seasons automatically</label></p>
<p class="hint">Turn this on and save to let outdoor temperature change the boiler settings.</p>
<div class="field"><label for="seasonLockout">Wait between season changes (hours, 0=skip)</label>
<input id="seasonLockout" type="number" min="0" max="99" step="1"></div>
<h3>Settings for each season</h3>
<p class="hint">Summer is on the warm side of Spring/Fall. Extra degrees keep it from switching back and forth.</p>
<div class="seasonTable"><table id="seasonEditor"></table></div>
<div id="seasonCards"></div>
<div id="seasonWarnings" class="seasonWarning" role="status"></div>
<div class="actions"><button id="seasonSave" class="primary" type="button">Save seasonal settings</button></div>
</details>
</fieldset>
<div class="actions"><button id="seasonReload" type="button">Reload saved settings</button>
<span id="seasonMessage" role="status"></span></div>`;
settingsRoot.appendChild(seasonalPanel);
const settingsSubtabs=document.createElement('div');
settingsSubtabs.className='settingsSubtabs';settingsSubtabs.setAttribute('role','tablist');
settingsSubtabs.setAttribute('aria-label','Settings categories');
for(const [id,label] of [['generalPanel','General'],['seasonalPanel','Seasonal'],['selfCleanSettings','Self-clean'],['sensors','Sensors'],['diagnostics','Diagnostic Test'],['notificationSettings','Network'],['sdLog','SD Log']]){
 const button=document.createElement('button');button.type='button';button.textContent=label;
 button.id=id+'Tab';button.setAttribute('role','tab');button.setAttribute('aria-controls',id);
 button.setAttribute('aria-selected',String(id==='generalPanel'));
 document.getElementById(id).setAttribute('aria-labelledby',button.id);
 button.onclick=()=>{
  [generalPanel,seasonalPanel,selfCleanSettings,sensorsPanel,diagnosticsPanel,notificationSettings,sdSection].forEach(panel=>panel.hidden=panel.id!==id);
  settingsSubtabs.querySelectorAll('button').forEach(b=>b.setAttribute('aria-selected',String(b===button)));
  if(id==='seasonalPanel'){seasonFetchedAt=0;pollNow()}
  if(id==='diagnostics')pollNow();
  if(id==='sensors'){if(lastState)renderProbes(lastState.water);pollNow()}
  if(id==='sdLog'){sdFetchedAt=0;pollNow()}
 };
 settingsSubtabs.appendChild(button);
}
settingsRoot.prepend(settingsSubtabs);
const settingsActions=document.createElement('div');settingsActions.className='actions';
settingsActions.append(document.getElementById('save'),document.getElementById('message'));
settingsRoot.appendChild(settingsActions);
const generalSeasonNote=document.createElement('p');generalSeasonNote.className='hint';
generalSeasonNote.textContent='Automatic Seasonal settings replace General exhaust, tank low/high, fan maximum and next-burn boost settings.';
generalPanel.prepend(generalSeasonNote);
const seasonEditor=document.getElementById('seasonEditor');
const seasonHead=document.createElement('thead'),seasonHeadRow=document.createElement('tr');
for(const label of ['Setting',...seasonNames]){
 const th=document.createElement('th');th.textContent=label;seasonHeadRow.appendChild(th);
}
seasonHead.appendChild(seasonHeadRow);seasonEditor.appendChild(seasonHead);
const seasonBody=document.createElement('tbody');
for(const field of seasonFields){
 const tr=document.createElement('tr'),th=document.createElement('th');th.textContent=field.label;tr.appendChild(th);
 seasonNames.forEach((name,index)=>{
  const td=document.createElement('td'),input=document.createElement('input');
  input.id='season'+index+'_'+field.key;input.type='number';input.step='1';input.min=field.min;input.max=field.max;
  input.setAttribute('aria-label',name+' '+field.label);
  if(index===0&&field.key==='start_f'){
   input.hidden=true;td.textContent='—';
  }
  td.appendChild(input);tr.appendChild(td);
 });
 seasonBody.appendChild(tr);
}
seasonEditor.appendChild(seasonBody);
const seasonInputHomes=[];
const seasonCards=document.getElementById('seasonCards');
seasonNames.forEach((name,index)=>{
 const card=document.createElement('div');card.className='seasonProfile';card.dataset.season=index;
 const heading=document.createElement('h3');heading.textContent=name;
 const badge=document.createElement('span');badge.className='seasonBadge';badge.textContent='Active season';badge.hidden=true;
 heading.appendChild(badge);card.appendChild(heading);
 seasonFields.forEach(field=>{
  if(index===0&&field.key==='start_f')return;
  const input=document.getElementById('season'+index+'_'+field.key);
  const wrapper=document.createElement('div');wrapper.className='seasonField';
  const label=document.createElement('label');label.htmlFor=input.id;
  label.textContent=field.label;
  wrapper.appendChild(label);card.appendChild(wrapper);
  seasonInputHomes.push({input,desktop:input.parentElement,mobile:wrapper});
 });
 seasonCards.appendChild(card);
});
const seasonPhoneLayout=matchMedia('(max-width:680px)');
function arrangeSeasonInputs(){
 seasonInputHomes.forEach(({input,desktop,mobile})=>(seasonPhoneLayout.matches?mobile:desktop).appendChild(input));
}
seasonPhoneLayout.addEventListener('change',arrangeSeasonInputs);
arrangeSeasonInputs();
let seasonSnapshot=null,seasonDirty=false,seasonSaving=false,seasonLoading=false,seasonAvailable=false,seasonFetchedAt=0;
let seasonSavedSignature='';
document.getElementById('env_units').addEventListener('change',syncSeasonUnits);
function seasonConfig(data){
 return {auto_enabled:data.auto_enabled,lockout_hours:data.lockout_hours,
  seasons:data.seasons.map(row=>Object.fromEntries(seasonFields.map(f=>[f.key,row[f.key]])))};
}
function seasonOutdoor(field){return field.key==='start_f'||field.key==='hysteresis_f'}
function seasonDisplay(value,field){return seasonMetric&&seasonOutdoor(field)?Number(((value-(field.key==='start_f'?32:0))*5/9).toFixed(2)):value}
function seasonLabel(field){return seasonMetric&&seasonOutdoor(field)?field.label.replaceAll('F','C'):field.label}
function seasonTemperature(value){return (seasonMetric?Number(((value-32)*5/9).toFixed(2)):value)+' '+(seasonMetric?'C':'F')}
function syncSeasonUnits(){
 const metric=document.getElementById('env_units').value==='1';
 if(metric===seasonMetric)return;
 const config=seasonSnapshot?seasonRead():null;
 seasonMetric=metric;
 seasonFields.forEach((field,row)=>{
  seasonBody.rows[row].cells[0].textContent=seasonLabel(field);
  seasonNames.forEach((name,i)=>{
   const input=document.getElementById('season'+i+'_'+field.key);
   input.min=seasonDisplay(field.min,field);input.max=seasonDisplay(field.max,field);
   input.step=seasonMetric&&seasonOutdoor(field)?'any':'1';
   input.setAttribute('aria-label',name+' '+seasonLabel(field));
   const home=seasonInputHomes.find(home=>home.input===input);
   if(home)home.mobile.querySelector('label').textContent=seasonLabel(field);
   if(config)input.value=Number.isFinite(config.seasons[i][field.key])?seasonDisplay(config.seasons[i][field.key],field):'';
  });
 });
 if(seasonSnapshot){renderSeasonLive(seasonSnapshot);renderSeasonPreview()}
}
function seasonRead(){
 const number=id=>{const value=document.getElementById(id).value;return value.trim()===''?NaN:Number(value)};
 return {auto_enabled:document.getElementById('seasonAuto').checked,
  lockout_hours:number('seasonLockout'),
  seasons:seasonNames.map((_,i)=>Object.fromEntries(seasonFields.map(f=>{
   const value=number('season'+i+'_'+f.key);
   return [f.key,seasonMetric&&seasonOutdoor(f)?
    (value<seasonDisplay(f.min,f)||value>seasonDisplay(f.max,f)?NaN:Math.round(value*9/5+(f.key==='start_f'?32:0))):value];
  })))};
}
function seasonInRange(p,f){
 return Number.isInteger(p[f.key])&&p[f.key]>=f.min&&p[f.key]<=f.max;
}
function seasonOrdered(p){
 return p[3].start_f<p[2].start_f&&p[2].start_f<p[1].start_f;
}
function seasonValidate(config,fanMinimum){
 const errors=[];
 if(typeof config.auto_enabled!=='boolean'||!Number.isInteger(config.lockout_hours)||
    config.lockout_hours<0||config.lockout_hours>99)    errors.push('Wait between season changes must be a whole number from 0 to 99 hours.');
 if(!Array.isArray(config.seasons)||config.seasons.length!==4)return ['Four seasonal profiles are required.'];
 config.seasons.forEach((p,i)=>{
  for(const f of seasonFields)if(!seasonInRange(p,f))
   errors.push(seasonNames[i]+': '+seasonLabel(f)+' must be '+seasonDisplay(f.min,f)+' to '+seasonDisplay(f.max,f)+
    (seasonMetric&&seasonOutdoor(f)?'.':' (whole numbers).'));
  if(p.tank_low_f>=p.tank_high_f)errors.push(seasonNames[i]+': tank low must be below tank high.');
  if(p.fan_max_pct<fanMinimum)errors.push(seasonNames[i]+': raise fan maximum to at least '+fanMinimum+'% (General fan minimum).');
  else if(p.boost_fan_pct<fanMinimum||p.boost_fan_pct>p.fan_max_pct)
   errors.push(seasonNames[i]+': set boost fan between '+fanMinimum+'% and '+p.fan_max_pct+'%.');
 });
 const p=config.seasons;
 if(!seasonOrdered(p))
  errors.push('Set the coldest outdoor changeover for Extreme, then Winter, then Spring/Fall.');
 return errors;
}
function seasonRules(profiles){
 const [summer,spring,winter,extreme]=profiles;
 return [
  {cool:spring.start_f-spring.hysteresis_f,warm:spring.start_f+summer.hysteresis_f,
   from:0,to:1},
  {cool:winter.start_f-winter.hysteresis_f,warm:winter.start_f+spring.hysteresis_f,
   from:1,to:2},
  {cool:extreme.start_f-extreme.hysteresis_f,warm:extreme.start_f+winter.hysteresis_f,
   from:2,to:3}
 ];
}
function seasonTrialSide(rule,value){
 return value<=rule.cool?rule.to:value>rule.warm?rule.from:null;
}
function renderSeasonPreview(){
 if(!seasonSnapshot)return;
 const config=seasonRead(),p=config.seasons;
 const errors=seasonValidate(config,seasonSnapshot.applied.fan_min_pct);
 const warning=document.getElementById('seasonWarnings');warning.textContent=errors.join('\n');
 document.getElementById('seasonSave').disabled=!seasonAvailable||seasonSaving||seasonLoading||!seasonDirty||errors.length>0;
 const bar=document.getElementById('seasonTransitions');
 seasonTrial.disabled=!seasonOrdered(p)||p.some((profile,i)=>seasonFields.some(f=>
  seasonOutdoor(f)&&(i!==0||f.key!=='start_f')&&!seasonInRange(profile,f)));
 bar.hidden=seasonTrial.disabled;
 if(seasonTrial.disabled){text('seasonTrialValue','');text('seasonBands','Check the outdoor temperatures under Edit seasonal settings.');return}
 const rule=seasonRules(p)[Number(seasonPair.value)], outdoor=seasonSnapshot.sensor_ok?seasonSnapshot.outdoor_f:null;
 const initial=Number.isFinite(outdoor)?Math.round(outdoor):p[rule.to].start_f;
 const min=Math.min(initial,rule.cool)-5,max=Math.max(initial,rule.warm)+5;
 const value=Math.max(min,Math.min(max,seasonTrialF??initial));
 seasonTrial.min=min;seasonTrial.max=max;seasonTrial.value=value;
 const shown=seasonTemperature(value);
 seasonTrial.setAttribute('aria-valuetext',shown);
 text('seasonTrialValue',shown);
 const side=seasonTrialSide(rule,value);
 text('seasonBands',(side===null?'Preview: no change between these two seasons.':'Preview: '+seasonNames[side])+
  '\n'+seasonTemperature(rule.cool)+' or colder: '+seasonNames[rule.to]+
  '\nAbove '+seasonTemperature(rule.warm)+': '+seasonNames[rule.from]);
 const pos=f=>100*(f-min)/(max-min)+'%';
 const marker=pos(value),cool=pos(rule.cool),warm=pos(rule.warm);
 bar.style.background=`linear-gradient(90deg,transparent calc(${marker} - 1px),white 0 calc(${marker} + 1px),transparent 0),linear-gradient(90deg,#4ba3ff ${cool},#566673 0 ${warm},#d18b53 0)`;
}
function renderSeasonLive(data){
 const p=data.applied;
 const active=seasonNames[data.active_season]||'Not selected';
 text('seasonLive','Outdoor: '+(data.sensor_ok&&Number.isFinite(data.outdoor_f)?seasonTemperature(data.outdoor_f):'No reading')+
  '\n'+(data.auto_enabled?'Automatic switching is on. Selected season: '+active+'.':'Automatic switching is off. No automatic changes are made.')+
  (data.auto_enabled&&data.lockout_remaining_s?'\nNext season change allowed in '+Math.ceil(data.lockout_remaining_s/60)+' min.':''));
 text('seasonApplied','Exhaust '+p.exhaust_f+' F | Tank '+p.tank_low_f+'–'+p.tank_high_f+' F | Fan '+p.fan_min_pct+'–'+p.fan_max_pct+'%'+
  '\nNext burn: delay '+p.damper_delay+' s | Boost '+p.boost_fan_pct+'% for '+p.boost_time+' s');
 [...seasonEditor.rows].forEach(row=>[...row.cells].forEach((cell,index)=>
  cell.classList.toggle('seasonActive',data.auto_enabled&&index>0&&index-1===data.active_season)));
 document.querySelectorAll('#seasonCards .seasonProfile').forEach(item=>{
  const active=data.auto_enabled&&Number(item.dataset.season)===data.active_season;
  item.classList.toggle('seasonActive',active);
  const badge=item.querySelector('.seasonBadge');if(badge)badge.hidden=!active;
 });
}
function fillSeasonForm(data){
 document.getElementById('seasonAuto').checked=data.auto_enabled;
 document.getElementById('seasonLockout').value=data.lockout_hours;
 data.seasons.forEach((p,i)=>seasonFields.forEach(f=>{document.getElementById('season'+i+'_'+f.key).value=seasonDisplay(p[f.key],f)}));
 seasonDirty=false;seasonSavedSignature=JSON.stringify(seasonConfig(data));
 document.getElementById('seasonFields').disabled=false;
}
async function loadSeasonal(replaceDraft=false){
 seasonLoading=true;document.getElementById('seasonReload').disabled=true;renderSeasonPreview();
 try{
  const data=await requestJson('/api/seasonal');
  if(typeof data.auto_enabled!=='boolean'||!Array.isArray(data.seasons)||data.seasons.length!==4||
     !Number.isInteger(data.lockout_hours)||!Number.isInteger(data.active_season)||
     !Number.isFinite(data.lockout_remaining_s)||typeof data.sensor_ok!=='boolean'||
     !data.applied||['exhaust_f','tank_low_f','tank_high_f','fan_min_pct','fan_max_pct','boost_time','boost_fan_pct','damper_delay'].some(k=>!Number.isFinite(data.applied[k])))
   throw new Error('Invalid seasonal settings response');
  if(data.seasons.some(p=>!p||seasonFields.some(f=>!Number.isInteger(p[f.key]))))
   throw new Error('Incomplete seasonal settings response');
  const signature=JSON.stringify(seasonConfig(data));
  const changed=seasonSnapshot&&signature!==seasonSavedSignature;
  seasonSnapshot=data;seasonAvailable=true;
  if(replaceDraft||!seasonDirty)fillSeasonForm(data);
  else if(changed)text('seasonMessage','Settings changed elsewhere. Draft kept; reload to compare.');
  seasonFetchedAt=Date.now();renderSeasonLive(data);
 }catch(error){seasonAvailable=false;throw error}
 finally{
  seasonLoading=false;document.getElementById('seasonReload').disabled=seasonSaving;renderSeasonPreview();
 }
}
async function refreshSeasonal(){
 if(!activeTab('settings')||seasonalPanel.hidden||document.hidden||seasonSaving||seasonLoading||Date.now()-seasonFetchedAt<5000)return;
 try{await loadSeasonal()}catch(error){
  seasonAvailable=false;seasonFetchedAt=Date.now();text('seasonLive','Unavailable - could not read seasonal settings.');
  text('seasonApplied','Unavailable.');
  text('seasonMessage',error.message);document.getElementById('seasonSave').disabled=true;
  throw error;
 }
}
const seasonTrial=document.getElementById('seasonTrial');
const seasonPair=document.getElementById('seasonPair');
let seasonTrialF=null;
document.getElementById('seasonFields').addEventListener('input',event=>{
 if(event.target===seasonTrial){seasonTrialF=seasonTrial.valueAsNumber;renderSeasonPreview();return}
 if(event.target===seasonPair){seasonTrialF=null;renderSeasonPreview();return}
 seasonDirty=true;text('seasonMessage','Unsaved seasonal changes.');renderSeasonPreview();
});
document.getElementById('seasonReload').onclick=runAction(async()=>{
 if(seasonDirty&&!confirm('Discard seasonal edits and reload saved settings?'))return;
 try{await loadSeasonal(true);text('seasonMessage','Reloaded saved seasonal settings.')}
 catch(error){seasonAvailable=false;text('seasonMessage',error.message);renderSeasonPreview();throw error}
});
document.getElementById('seasonSave').onclick=runAction(async()=>{
 if(!seasonSnapshot||!seasonAvailable||seasonSaving||seasonLoading)return;
 const config=seasonRead(),errors=seasonValidate(config,seasonSnapshot.applied.fan_min_pct);
 if(errors.length){text('seasonMessage',errors.join(' '));return}
 if(!confirm('Save all seasonal profiles and auto switch? ON replaces General exhaust, tank, fan maximum and next-burn damper delay/boost speed/time.'))return;
 const password=prompt('Enter the Control/API Password to confirm saving seasonal settings');
 if(password===null||password===''){text('seasonMessage','Save cancelled.');return}
 localStorage.setItem(tokenKey,password);
 seasonSaving=true;document.getElementById('seasonFields').disabled=true;
 document.getElementById('seasonReload').disabled=true;
 try{
  const result=await requestJson('/api/seasonal',{method:'POST',
   headers:{'Content-Type':'application/json','X-Boiler-Token':password},body:JSON.stringify(config)});
  if(!result.ok)throw new Error(result.error||'Seasonal settings were not saved');
  seasonDirty=false;lastSettingsMs=0;
  text('seasonMessage','Saved. Refreshing...');
  try{await loadSeasonal(true);text('seasonMessage','Saved. Applied values update in about 3 seconds.')}
  catch(error){seasonAvailable=false;seasonFetchedAt=0;throw new Error('Settings were saved, but refreshing failed: '+error.message)}
 }catch(error){
  text('seasonMessage',error.message);throw error;
 }finally{
  seasonSaving=false;document.getElementById('seasonFields').disabled=false;
  document.getElementById('seasonReload').disabled=false;renderSeasonPreview();
 }
});
</script>
)seasonhtml"
