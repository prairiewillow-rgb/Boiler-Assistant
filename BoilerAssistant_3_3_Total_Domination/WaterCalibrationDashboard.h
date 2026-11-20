R"watercal(
<script>
const waterCalPanel=document.createElement('details');
waterCalPanel.innerHTML=`<summary>Probe calibration</summary>
<p class="hint">Offsets apply everywhere, including fan control, over-temperature safety,
heat calculations and future logs. Compare settled probes against a trusted reference
at the same temperature. An offset cannot fix wiring faults or an unstable probe.
Uncalibrated readings below use the existing smoothing, not an instantaneous raw sample.
Range: -10 to +10 F. Historical data is not rewritten.</p>
<div id="waterCalCards" class="grid"></div><p id="waterCalStatus" role="status"></p>`;
document.getElementById('sensors').appendChild(waterCalPanel);
const waterCalDrafts=new Map();
let waterCalSaving=false;
waterCalPanel.addEventListener('toggle',()=>{if(waterCalPanel.open)pollNow()});
function waterCalValue(v){
 return Number.isFinite(v)?v.toFixed(2)+' F':'Unavailable';
}
function renderWaterCalibration(data){
 if(data.error)throw new Error(data.error);
 const root=document.getElementById('waterCalCards');
 const ids=new Set(data.probes.map(p=>p.id));
 for(const [id,draft] of waterCalDrafts){
  if(!ids.has(id)){draft.card.remove();waterCalDrafts.delete(id)}
 }
 for(const p of data.probes){
  let draft=waterCalDrafts.get(p.id);
  if(!draft){
   const card=document.createElement('div');card.className='card';
   const title=document.createElement('h3'),readings=document.createElement('p');
   const label=document.createElement('label'),input=document.createElement('input');
   input.type='number';input.min='-10';input.max='10';input.step='0.1';
   input.style.cssText='width:100%;min-height:44px;font-size:16px';
   input.id='waterCal-'+p.id;label.htmlFor=input.id;label.textContent='Offset (F)';
   const preview=document.createElement('p'),button=document.createElement('button');
   button.textContent='Save offset';button.style.minHeight='44px';
   draft={card,title,readings,input,preview,button,dirty:false,probe:p};
   input.oninput=()=>{draft.dirty=true;previewWaterCalibration(draft)};
   button.onclick=()=>saveWaterCalibration(p.id).catch(error=>{
    text('waterCalStatus',error.message);reportRequestError(error);
   });
   card.append(title,readings,label,input,preview,button);root.appendChild(card);
   waterCalDrafts.set(p.id,draft);
  }
  draft.probe=p;
  draft.title.textContent=p.name+(p.controls_tank?' (controls tank)':'')+' ['+p.id+']';
  draft.readings.textContent='Uncalibrated: '+waterCalValue(p.uncalibrated_f)+
   '; applied offset: '+waterCalValue(p.offset_f)+'; corrected: '+waterCalValue(p.corrected_f);
  if(!draft.dirty&&document.activeElement!==draft.input)draft.input.value=p.offset_f;
  draft.button.disabled=!data.storage_available||waterCalSaving;
  previewWaterCalibration(draft);
 }
 text('waterCalStatus',!data.storage_available?'Calibration storage unavailable':
  !data.probes.length?'No water probes connected.':'Offsets are saved by unique probe ID.');
}
function previewWaterCalibration(draft){
 const n=Number(draft.input.value);
 const valid=draft.input.value.trim()!==''&&Number.isFinite(n)&&Math.abs(n)<=10;
 draft.preview.textContent=!valid?'Enter an offset between -10 and +10 F.':
  'Preview corrected: '+waterCalValue(Number.isFinite(draft.probe.uncalibrated_f)?
   draft.probe.uncalibrated_f+n:null);
 return valid;
}
async function refreshWaterCalibration(){
 if(document.hidden||!activeTab('sensors')||!waterCalPanel.open||waterCalSaving)return;
 try{renderWaterCalibration(await requestJson('/api/water_calibration'))}
 catch(error){
  for(const d of waterCalDrafts.values()){
   d.probe={...d.probe,uncalibrated_f:null,corrected_f:null};
   d.readings.textContent='Unavailable - calibration refresh failed';
   d.preview.textContent='Unavailable';d.button.disabled=true;
  }
  text('waterCalStatus',error.message);throw error;
 }
}
async function saveWaterCalibration(id){
 const draft=waterCalDrafts.get(id);
 if(waterCalSaving)return;
 if(!previewWaterCalibration(draft))throw new Error('Offset must be between -10 and +10 F');
 if(!confirm('Apply this offset everywhere, including boiler control and safety thresholds?'))return;
 const password=prompt('Enter the Control/API Password to save calibration');
 if(!password){text('waterCalStatus','Save cancelled');return}
 waterCalSaving=true;
 for(const d of waterCalDrafts.values())d.button.disabled=true;
 try{
  const offset=Number(draft.input.value);
  const result=await requestJson('/api/water_calibration',{method:'POST',
   headers:{'Content-Type':'application/json','X-Boiler-Token':password},
   body:JSON.stringify({id,offset_f:offset})});
  if(!result.ok)throw new Error(result.error||'Calibration rejected');
  draft.dirty=Number(draft.input.value)!==offset;
  renderWaterCalibration(await requestJson('/api/water_calibration'));
  text('waterCalStatus','Saved. Corrected readings now apply everywhere; heat intervals restarted.');
 }finally{
  waterCalSaving=false;
  // A failed read leaves saving disabled until a successful refresh.
  pollNow();
 }
}
</script>
)watercal"
