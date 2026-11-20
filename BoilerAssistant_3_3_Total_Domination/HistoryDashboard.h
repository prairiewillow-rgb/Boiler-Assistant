/*
 * Main live/SD water/exhaust/fan chart and SD Burn Times history.
 * Adjacent string literal included by DashboardHTML.h.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
R"historyhtml(
<style>
#historyViewport{touch-action:pan-x pan-y;position:relative;scrollbar-width:none;-ms-overflow-style:none}
#historyViewport::-webkit-scrollbar{display:none}
#state #historyViewport{margin:8px 0;height:220px}
#performance{margin:8px 0 12px}
#performance h2{margin:8px 0;font-size:1.1rem}
.historyControls{margin:12px 0}
#historyTrack{height:100%}
#burnGraph{position:sticky;left:0;min-width:0}
.historyDrag{cursor:grab;user-select:none;touch-action:pan-x pan-y}
.historyDrag.dragging{cursor:grabbing}
#history .historyRow{grid-template-columns:30px repeat(3,minmax(0,1fr))}
#history .historyRow .historyValue{display:block}
.historyControls{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;align-items:center}
.historyControls button{min-width:0;min-height:44px;padding:10px 4px;font-size:clamp(11px,3vw,16px);white-space:nowrap}
.historyControls button[aria-pressed="true"]{outline:2px solid #55c4c7}
.timeScale[hidden]{display:none}
.mainChartLegend{display:flex;flex-wrap:wrap;gap:12px;font-size:.85rem}
.mainChartLegend span{border-left:4px solid;padding-left:7px}
.mainChartLegend .liveTrendKey{border-left-style:dashed}
@media(max-width:600px){
 #state #historyViewport,#performanceViewport{height:280px}
 .mainChartLegend,#performance .legend{gap:8px 14px;font-size:.78rem}
 #performance h2{margin-top:16px}
}
.card,.selfCleanPanel{background:linear-gradient(145deg,var(--card),var(--panel))}
button,.envItem{background:#3c515e}
.field input,.field select,#seasonalPanel input[type=number]{background:#293b46}
.historyViewport,.historyGraph,#performanceViewport{background:#2c3d48}
.historySlider{display:flex;align-items:center;gap:8px;max-width:560px;margin:8px auto;width:100%;min-width:0}
.historySlider input{flex:1;min-width:0;width:0;height:44px;margin:0;accent-color:var(--cyan)}
.historySlider button{padding:8px;min-height:44px}
#diagnosticReport{min-height:420px!important;white-space:pre-wrap;overflow-wrap:anywhere}
#copyDiagnostics{margin:12px 8px 0 0}
</style>
<script>
const HISTORY_SECONDS=86400,BURN_HISTORY_SECONDS=86400;
const HISTORY_BLOCK_BYTES=4096;
let historyLoadPromise=null,historyLoading=false,historyLoadError='';
let historyBusyRetryAt=0;
const HISTORY_LOADING_ENABLED=true;
// Only the visible hour is read first; older SD data is fetched when the view needs it.
const HISTORY_DEPTH_STEP=3600,HISTORY_DEPTH_MARGIN=600;
let historyLoadedDepth=0;
let historyViewSeconds=3600;
let historyViewEndUtc=null;
let historyFollowLatest=true;
let historyScrollFrame=null;
const liveTrend=[];
function latestTrendTime(){
 return Math.max(historySnapshot?.now_utc||0,liveTrend.at(-1)?.[0]||0);
}
function sampleLiveTrend(s){
 if(!HISTORY_LOADING_ENABLED||!activeTab('state'))return;
 const utc=Math.floor(Date.now()/1000),previous=liveTrend.at(-1)?.[0];
 if(utc<previous)liveTrend.length=0;
 else if(utc-previous<5)return;
 const probes={};
 for(const p of s.water||[])if(p.id)probes[p.id.toUpperCase()]=p.sensor_ok?p.temp_f:null;
 liveTrend.push([utc,s.tank_sensor_ok?s.tank_temp:null,s.exhaust_sensor_ok&&!s.exhaust_fallback?s.exhaust_smooth:null,s.fan_final,probes]);
 while(liveTrend[0][0]<utc-360)liveTrend.shift();
 if(historyFollowLatest||!historySnapshot?.available)positionHistoryChart();
}
const historyViewport=document.getElementById('historyViewport');
const historyCanvas=document.getElementById('burnGraph');
const historyTrack=document.createElement('div');
historyTrack.id='historyTrack';
historyViewport.appendChild(historyTrack);
historyTrack.appendChild(historyCanvas);
historyViewport.tabIndex=0;
historyViewport.setAttribute('aria-label','Chart time: swipe or use arrow keys.');
document.querySelector('.mainWaterHint').hidden=true;
const chartDescription='Dashed: live. Muted red: connects across missing exhaust readings; not measured. Historical fan envelope is not actual output.';
const damperDescription='The purple line shows recorded damper OPEN commands, not measured position. Closed or unknown intervals have no line.';
const damperLegend=document.createElement('span');
damperLegend.style.borderColor='#c3a6ff';damperLegend.textContent='Damper open';
damperLegend.title=damperDescription;
const liveTrendKey=document.createElement('span');
liveTrendKey.className='liveTrendKey';liveTrendKey.textContent='Live';
document.querySelector('.mainChartLegend').append(damperLegend,liveTrendKey);
historyCanvas.setAttribute('title',chartDescription+' '+damperDescription);
historyCanvas.setAttribute('aria-description',chartDescription+' '+damperDescription);
const historyStatus=document.createElement('div');
historyStatus.id='waterHistoryStatus';historyStatus.className='historyHint';
historyStatus.setAttribute('role','status');
historyViewport.before(historyStatus);
const historyControls=document.createElement('div');
historyControls.className='historyControls';
const historyViewButtons=[1,12,24].map(hours=>{
 const button=document.createElement('button');
 button.textContent='Last '+hours+(hours===1?' hour':' hours');
 button.setAttribute('aria-pressed',String(hours===1));
 button.onclick=()=>{
  historyViewSeconds=hours*3600;
  historyViewButtons.forEach(b=>b.setAttribute('aria-pressed',String(b===button)));
  historyFollowLatest=true;positionHistoryChart();
 };
 return button;
});
historyControls.append(...historyViewButtons);
historyViewport.after(historyControls);
const historyScroller=document.createElement('input');
historyScroller.type='range';historyScroller.min=0;historyScroller.max=10000;historyScroller.step=1;
historyScroller.value=10000;
historyScroller.setAttribute('aria-label','Shared chart time: oldest to latest');
historyScroller.oninput=()=>{
 historyViewport.scrollLeft=Number(historyScroller.value)/10000*(historyViewport.scrollWidth-historyViewport.clientWidth);
 historyViewport.dispatchEvent(new Event('scroll'));
};
const historySlider=document.createElement('div');historySlider.className='historySlider';
const historyOldest=document.createElement('button'),historyLatest=document.createElement('button');
historyOldest.textContent='Oldest';historyLatest.textContent='Latest';
historyOldest.onclick=()=>{historyScroller.value=0;historyScroller.oninput()};
historyLatest.onclick=()=>{historyScroller.value=10000;historyScroller.oninput()};
historySlider.append(historyOldest,historyScroller,historyLatest);
historyViewport.after(historySlider);
historySlider.after(performanceSection);
performanceSection.after(historyControls);
function renderLinkedHeatChart(){
 const s=historySnapshot;
 renderPerformance({...s,now_utc:s?.now_utc||latestTrendTime(),samples:s?.available?s.loops||[]:[]});
}
document.querySelector('.timeScale').hidden=true;

function historyLocalTime(utc,withDate=false,offset=historySnapshot?.utc_offset_minutes||0){
 return new Date((utc+offset*60)*1000).toLocaleString(undefined,{
  timeZone:'UTC',...(withDate?{month:'short',day:'numeric'}:{}),hour:'2-digit',minute:'2-digit'
 });
}
const historyPlotLeft=4,historyPlotRight=67;
function historyPlotX(width,start){
 return utc=>historyPlotLeft+(utc-start)/historyViewSeconds*(width-historyPlotLeft-historyPlotRight);
}
function drawHistoryTimeAxis(ctx,start,end,ticks,x,height){
 const offset=historySnapshot?.utc_offset_minutes??-new Date().getTimezoneOffset();
 ctx.textAlign='center';ctx.fillStyle='#bdcdd5';
 for(let i=0;end&&i<=ticks;i++){
  const utc=start+historyViewSeconds*i/ticks;
  ctx.fillText(historyLocalTime(utc,false,offset),Math.max(25,x(utc)),height-9);
 }
}
function enableHistoryDrag(viewport){
 viewport.classList.add('historyDrag');
 let drag=null;
 viewport.addEventListener('pointerdown',event=>{
  if(event.pointerType!=='mouse'||event.button!==0||viewport.scrollWidth<=viewport.clientWidth)return;
  drag={id:event.pointerId,x:event.clientX,left:viewport.scrollLeft};
  viewport.setPointerCapture(event.pointerId);
  viewport.classList.add('dragging');event.preventDefault();viewport.focus();
 });
 viewport.addEventListener('pointermove',event=>{
  if(drag&&event.pointerId===drag.id)viewport.scrollLeft=drag.left+drag.x-event.clientX;
 });
 const stop=()=>{drag=null;viewport.classList.remove('dragging')};
 viewport.addEventListener('pointerup',stop);
 viewport.addEventListener('pointercancel',stop);
 viewport.addEventListener('lostpointercapture',stop);
 viewport.addEventListener('keydown',event=>{
  if(event.key!=='ArrowLeft'&&event.key!=='ArrowRight')return;
  event.preventDefault();
  viewport.scrollBy({left:viewport.clientWidth*(event.key==='ArrowLeft'?-1:1),behavior:'smooth'});
 });
}
function parsePerformanceRecord(fields,snapshot,windowSeconds){
 const utc=Number(fields[0]);
 if(!(fields[1]==='Q'?[17,18].includes(fields.length):[14,17].includes(fields.length))||!fields[0]||!Number.isSafeInteger(utc)||utc<=0){snapshot.invalid_rows++;return null}
 if(utc<snapshot.now_utc-windowSeconds||utc>snapshot.now_utc&&!snapshot.readAhead)return null;
 const row=fields.slice(2).map(v=>v===''?null:Number(v));
 const hasDamper=fields[1]==='Q'&&fields.length===18;
 const damper=hasDamper?row.pop():null;
 if(hasDamper&&damper!==null&&damper!==0&&damper!==1){snapshot.invalid_rows++;return null}
 if(row.some(v=>v!==null&&!Number.isFinite(v))||
    row.slice(2,4).some(v=>v===null||!Number.isInteger(v))||
    row.slice(6,12).some(v=>v===null||!Number.isInteger(v))||
    row[2]<0||row[2]>100||row[3]<0||row[3]>4||
    row.slice(0,2).some(v=>v!==null&&(v< -400||v>3000))){
  snapshot.invalid_rows++;return null;
 }
 while(row.length<15)row.push(null);
 if(fields[1]==='P')row.fill(null,12);
 const result=[utc,...row,fields[1]==='Q'?15:60];
 if(hasDamper)result.push(damper);
 return result;
}
function shadeActiveBurns(ctx,points,start,end,x,top,height){
 ctx.fillStyle='rgba(160,230,174,0.22)';
 points.forEach(([utc,state,step],i)=>{
  if(![1,2,3].includes(state))return;
  const a=Math.max(start,utc),b=Math.min(end,utc+step,points[i+1]?.[0]??end);
  if(b>a)ctx.fillRect(x(a),top,x(b)-x(a),height);
 });
}
function parseHistoryRow(line,snapshot){
 if(line.startsWith('D,')||line.startsWith('S,')||line==='utc,type,duration_s,interval_s,tank_f')return;
 const fields=line.split(',');
 if(fields[1]==='T'){
  const utc=Number(fields[0]),values={};
  if(fields.length<4||fields.length>18||fields.length%2||
     !/^[0-9]+$/.test(fields[0])||!Number.isSafeInteger(utc)||utc<=0){
   snapshot.invalid_rows++;return;
  }
  for(let i=2;i<fields.length;i+=2){
   const id=fields[i].toUpperCase(),temp=fields[i+1]===''?null:Number(fields[i+1]);
   if(!/^[0-9A-F]{16}$/.test(id)||id in values||
      (temp!==null&&(!fields[i+1].trim()||!Number.isFinite(temp)||temp< -200||temp>400))){
    snapshot.invalid_rows++;return;
   }
   values[id]=temp;
  }
  if(utc>=snapshot.now_utc-HISTORY_SECONDS&&(utc<=snapshot.now_utc||snapshot.readAhead))
   (snapshot.loops??=[]).push([snapshot.now_utc-utc,values]);
  return;
 }
 if(fields[1]==='P'||fields[1]==='Q'){
  const row=parsePerformanceRecord(fields,snapshot,HISTORY_SECONDS);
  if(row){
   const water=[...row.slice(0,5),row[16],row[9],row[10]];
   if(row.length===18)water.push(row[17]);
   snapshot.water.push(water);
  }
  return;
 }
 const utc=Number(fields[0]);
 if(fields.length!==5||!fields[0]||!Number.isSafeInteger(utc)||utc<=0||
    !['W','B'].includes(fields[1])){snapshot.invalid_rows++;return}
 if(utc<snapshot.now_utc-BURN_HISTORY_SECONDS||utc>snapshot.now_utc&&!snapshot.readAhead)return;
 const tank=fields[4]===''?null:Number(fields[4]);
 if(tank!==null&&(!Number.isFinite(tank)||tank< -200||tank>400)){
  snapshot.invalid_rows++;return;
 }
 if(fields[1]==='W'){
  if(utc>=snapshot.now_utc-HISTORY_SECONDS)snapshot.water.push([utc,tank]);return;
 }
 const duration=Number(fields[2]),interval=fields[3]===''?null:Number(fields[3]);
 if(fields[2]===''||!Number.isSafeInteger(duration)||duration<0||
    (interval!==null&&(!Number.isSafeInteger(interval)||interval<0))){
  snapshot.invalid_rows++;return;
 }
 snapshot.burns.push({end_utc:utc,duration_sec:duration,interval_sec:interval,water_temp_f:tank});
}
const historyFileCache=new Map();
function requestHistoryFile(path,snapshot,parseRow=parseHistoryRow,offset=0,count=0){
 const run=async()=>{
  const controller=new AbortController();let timer;
  const deadline=setTimeout(()=>controller.abort(),3000);
  const touch=()=>{clearTimeout(timer);timer=setTimeout(()=>controller.abort(),REQUEST_TIMEOUT_MS)};
  let reader;
  try{
   touch();
   const response=await fetch('/api/sd/file?f='+encodeURIComponent(path)+(offset?'&start='+offset:'')+(count?'&count='+count:''),{
    cache:'no-store',signal:controller.signal
   });
   if(!response.ok)throw Object.assign(new Error('SD history file unavailable (HTTP '+response.status+')'),{status:response.status});
   if(offset&&response.headers.get('X-SD-Offset')!==String(offset))throw new Error('Invalid SD offset');
   if(!response.body)throw new Error('Streaming unavailable');
   reader=response.body.getReader();
   const decoder=new TextDecoder();let pending='',bytes=0,complete=0;
   while(true){
    const {value,done}=await reader.read();if(done)break;touch();
    const newline=value.lastIndexOf(10);
    if(newline>=0)complete=bytes+newline+1;
    bytes+=value.length;
    pending+=decoder.decode(value,{stream:true});
    let end;
    while((end=pending.indexOf('\n'))>=0){
     const line=pending.slice(0,end).replace(/\r$/,'');
     pending=pending.slice(end+1);
     if(line||count)parseRow(line,snapshot);
    }
    if(pending.length>(path.startsWith('LOGS/')?420:256))throw new Error('Invalid SD history row length');
   }
   pending+=decoder.decode();
   if(count&&bytes!==count)throw new Error('Incomplete SD history block');
   if(!count&&pending.trim())snapshot.invalid_rows++;
   return {offset:offset+complete,size:offset+bytes,partial:!!pending.trim(),tail:pending};
  }finally{
   clearTimeout(timer);clearTimeout(deadline);
   if(reader){try{await reader.cancel()}finally{reader.releaseLock()}}
  }
 };
 // Serialize each SD read, but let live polling use the queue during busy-card backoff.
 return (async()=>{
  for(let tries=0;;tries++){
   const result=requestQueue.then(run);
   requestQueue=result.catch(()=>{});
   try{return await result}catch(error){
    if(error.status!==503||tries>=4)throw error;
    await new Promise(resolve=>setTimeout(resolve,400+tries*300));
   }
  }
 })();
}
function publishHistory(data){
 const snapshot={...data,water:[],burns:[],loops:[],invalid_rows:0};
 for(const [path] of data.files){
  const part=historyFileCache.get(path);if(!part)continue;
  snapshot.water.push(...part.water.filter(row=>row[0]>=data.now_utc-HISTORY_SECONDS&&row[0]<=data.now_utc));
  snapshot.burns.push(...part.burns.filter(row=>row.end_utc>=data.now_utc-BURN_HISTORY_SECONDS&&row.end_utc<=data.now_utc));
  snapshot.loops.push(...part.loops.map(row=>[row[0]+data.now_utc-part.now_utc,...row.slice(1)])
   .filter(row=>row[0]>=0&&row[0]<=HISTORY_SECONDS));
  snapshot.invalid_rows+=part.invalid_rows;
 }
 const points=new Map();
 snapshot.water.forEach(row=>{if(!points.has(row[0])||row.length>2)points.set(row[0],row)});
 snapshot.water=Array.from(points.values()).sort((a,b)=>a[0]-b[0]);
 snapshot.loops.sort((a,b)=>b[0]-a[0]);
 snapshot.burns.sort((a,b)=>b.end_utc-a.end_utc);
 historySnapshot=snapshot;
}
function historyPaused(){
 return document.hidden||(!activeTab('state')&&!activeTab('history'))||!updateConnectionStatus();
}
function historyDepthNeeded(now){
 if(activeTab('history'))return BURN_HISTORY_SECONDS;
 const seconds=now-(historyViewEndUtc??now)+historyViewSeconds;
 return Math.min(HISTORY_SECONDS,Math.max(HISTORY_DEPTH_STEP,Math.ceil(seconds/HISTORY_DEPTH_STEP)*HISTORY_DEPTH_STEP));
}
function historyNeedsDeeperLoad(){
 return !!historySnapshot?.available&&!historyLoadError&&historyDepthNeeded(historySnapshot.now_utc)>historyLoadedDepth;
}
function oldestHistoryUtc(rows,nowUtc){
 let oldest=Infinity;
 for(const row of rows.water)oldest=Math.min(oldest,row[0]);
 for(const row of rows.burns)oldest=Math.min(oldest,row.end_utc);
 for(const row of rows.loops)oldest=Math.min(oldest,nowUtc-row[0]);
 return oldest;
}
async function loadHistory(data){
 historyLoading=true;historyLoadError='';
 let depth=historyDepthNeeded(data.now_utc);
 let lastPublish=-Infinity,dirty=false;
 const publish=()=>{publishHistory(data);lastPublish=Date.now();dirty=false};
 const needsOlder=part=>{
  // Opening Burn Times can upgrade an in-flight Main chart load.
  if(activeTab('history'))depth=BURN_HISTORY_SECONDS;
  return depth===BURN_HISTORY_SECONDS||part.oldestUtc>data.now_utc-depth-HISTORY_DEPTH_MARGIN;
 };
 try{
  for(const file of [...data.files].sort((a,b)=>b[0].slice(-12).localeCompare(a[0].slice(-12))||b[0].localeCompare(a[0]))){
   if(historyPaused())return;
   const cached=historyFileCache.get(file[0]);
   const reuse=cached&&cached.size<=file[1]&&cached.now_utc<=data.now_utc;
   const part=reuse?{...cached,water:[...cached.water],loops:[...cached.loops],burns:[...cached.burns]}:
    {now_utc:data.now_utc,water:[],burns:[],loops:[],invalid_rows:0,offset:0,size:file[1],remaining:file[1],leading:'',partial:false,oldestUtc:Infinity};
   if(reuse){
    part.loops=part.loops.map(row=>[row[0]+data.now_utc-part.now_utc,...row.slice(1)]);
    part.now_utc=data.now_utc;
   }
   part.readAhead=true;
   while(needsOlder(part)&&part.remaining>0){
    if(historyPaused())return;
    const end=part.remaining,start=Math.max(0,end-HISTORY_BLOCK_BYTES);
    let first=true,leading='';
    const rows={now_utc:part.now_utc,readAhead:true,water:[],loops:[],burns:[],invalid_rows:0};
    const progress=await requestHistoryFile(file[0],rows,(line,snapshot)=>{
     if(first&&start>0)leading=line;
     else if(line)parseHistoryRow(line,snapshot);
     first=false;
    },start,end-start);
    if(first&&start>0)leading=progress.tail;
    else if(end<part.size){
     const line=(progress.tail+part.leading).replace(/\r$/,'');
     if(line)parseHistoryRow(line,rows);
    }
    if(end===part.size){
     part.offset=progress.offset;part.partial=progress.partial;
     if(progress.partial)rows.invalid_rows++;
    }
    part.leading=leading;part.remaining=start;
    part.water.unshift(...rows.water);part.loops.unshift(...rows.loops);part.burns.unshift(...rows.burns);
    part.invalid_rows+=rows.invalid_rows;
    part.oldestUtc=Math.min(part.oldestUtc,oldestHistoryUtc(rows,part.now_utc));
    historyFileCache.set(file[0],part);
    dirty=true;
    if(Date.now()-lastPublish>=150){publish();renderHistorySnapshot()}
    await new Promise(resolve=>setTimeout(resolve,0));
   }
   if(reuse&&(file[1]!==part.size||part.partial)){
    const added={now_utc:part.now_utc,readAhead:true,water:[],loops:[],burns:[],invalid_rows:0};
    const progress=await requestHistoryFile(file[0],added,parseHistoryRow,part.offset);
    part.water.push(...added.water);part.loops.push(...added.loops);part.burns.push(...added.burns);
    part.invalid_rows+=added.invalid_rows-(part.partial?1:0);
    Object.assign(part,progress);
   }
   part.water=part.water.filter(row=>row[0]>=data.now_utc-HISTORY_SECONDS);
   part.loops=part.loops.filter(row=>row[0]<=HISTORY_SECONDS);
   part.burns=part.burns.filter(row=>row.end_utc>=data.now_utc-BURN_HISTORY_SECONDS);
   historyFileCache.set(file[0],part);
   dirty=true;
   if(!needsOlder(part))break;
  }
  publish();
  historyLoadedDepth=depth;
  lastHistoryMs=Date.now();
 }catch(error){
  console.error('History unavailable:',error);
  if(dirty)publish();
  historyLoadError='History read failed - '+error.message;
  lastHistoryMs=Date.now();
  if(!historySnapshot?.water.length&&!historySnapshot?.burns.length)historySnapshot={available:false,reason:historyLoadError,water:[],burns:[]};
 }finally{
  if(dirty)publish();
  historyLoading=false;historyLoadPromise=null;renderHistorySnapshot();
 }
}
async function refreshCharts(){
 if(!HISTORY_LOADING_ENABLED){
  text('waterHistoryStatus','Charts disabled for isolation test');
  text('burnHistoryStatus','History disabled for isolation test');
  return;
 }
 if(historyPaused())return;
 if(historyLoadPromise)return;
 if(Date.now()<historyBusyRetryAt)return;
 if(historyNeedsDeeperLoad())lastHistoryMs=0;
 if(!historyBusyRetryAt&&lastHistoryMs&&Date.now()-lastHistoryMs<SLOW_REFRESH_MS){renderHistorySnapshot();return}
 historyBusyRetryAt=0;
 historyLoadError='';
 text('waterHistoryStatus','Reading SD history...');
 text('burnHistoryStatus','Reading SD history...');
 try{
  const data=await requestJson('/api/history');
  if(!data.available&&data.reason==='SD batch in progress; retry shortly'){
   historyBusyRetryAt=Date.now()+2000;
   renderHistorySnapshot();
   return;
  }
  if(data.available){
   if(!Number.isSafeInteger(data.now_utc)||data.now_utc<=0||!Array.isArray(data.files))
    throw new Error('Invalid SD history manifest');
   for(const key of historyFileCache.keys())if(!data.files.some(f=>f[0]===key))historyFileCache.delete(key);
   for(const file of data.files){
    if(!Array.isArray(file)||!/^(HISTORY|LOGS)\/[0-9]{8}\.CSV$/.test(file[0])||!Number.isSafeInteger(file[1])||file[1]<0)
     throw new Error('Invalid SD history file name');
   }
   historyLoadPromise=Promise.resolve().then(()=>loadHistory(data));
   return;
  }
  historySnapshot={...data,water:[],burns:[]};
  if(!data.available){historyFileCache.clear();historyLoadedDepth=0}
 }catch(error){
  console.error('History unavailable:',error);
  historySnapshot={available:false,reason:'History read failed - '+error.message,water:[],burns:[]};
  historyFileCache.clear();historyLoadedDepth=0;
 }
 lastHistoryMs=Date.now();
 renderHistorySnapshot();
}
function renderHistorySnapshot(){
 if(historyBusyRetryAt){
  const message='SD is writing. Retrying history shortly...';
  text('waterHistoryStatus',message);text('burnHistoryStatus',message);
  historyStatus.hidden=false;
  if(!historySnapshot)return;
  if(activeTab('state'))positionHistoryChart();
  if(activeTab('history'))renderSdBurnTimes();
  return;
 }
 if(!historySnapshot)return;
 const s=historySnapshot;
 const message=historyLoadError||(!s.available?'History unavailable: '+(s.reason||'No working SD card'):
  historyLoading?'Loading recent readings first; older history follows...':
  !s.water.length&&!s.burns.length?'No SD history yet.':
  'Recording gaps.'+
   (s.invalid_rows?' '+s.invalid_rows+' damaged/incomplete rows skipped.':''));
 const ok=s.available&&(s.water.length||s.burns.length);
 const hours=historyLoadedDepth/3600;
 const loaded=hours?'Last '+hours+(hours===1?' hour':' hours')+' from SD. ':'From SD. ';
 text('waterHistoryStatus',(ok?loaded:'')+message);
 historyStatus.hidden=!!ok&&!s.invalid_rows&&!historyLoading&&!historyLoadError;
 const burnsComplete=historyLoadedDepth>=BURN_HISTORY_SECONDS&&!historyLoading&&!historyLoadError;
 text('burnHistoryStatus',!s.available||historyLoadError?message:burnsComplete?
  'Last 24 hours from SD.'+(s.invalid_rows?' Some incomplete rows skipped.':''):
  'Loading the last 24 hours from SD...');
 historyViewButtons.forEach(button=>button.disabled=!s.available);
 if(activeTab('state'))positionHistoryChart();
 if(activeTab('history'))renderSdBurnTimes();
}
function positionHistoryChart(){
 const width=historyViewport.clientWidth;
 if(!width)return;
 historyTrack.style.width=(historySnapshot?.available?width*HISTORY_SECONDS/historyViewSeconds:width)+'px';
 historyCanvas.style.width=width+'px';
 if(historySnapshot?.available){
  const now=latestTrendTime();
  if(historyFollowLatest||historyViewEndUtc===null)historyViewEndUtc=now;
  historyViewEndUtc=Math.max(now-HISTORY_SECONDS+historyViewSeconds,Math.min(now,historyViewEndUtc));
  const span=HISTORY_SECONDS-historyViewSeconds;
  const fraction=span>0?(historyViewEndUtc-(now-span))/span:1;
  historyScroller.value=Math.round(fraction*10000);
  historyScroller.disabled=span<=0;
  historyViewport.scrollLeft=fraction*(historyViewport.scrollWidth-width);
 }
 if(!historySnapshot?.available){
  historyViewEndUtc=latestTrendTime();historyScroller.disabled=true;
 }
 drawSdWaterChart();
}
function smoothChartReadings(points,columns,stepColumn,direction=1,smoothingSeconds=60){
 const filtered=[],previous=[];
 let lastStep=60;
 return points.map(row=>{
  const utc=row[0]*direction,copy=[...row];
  columns.forEach(column=>{
   const value=row[column],dt=utc-previous[column];
   if(typeof value!=='number'||!Number.isFinite(value)){
    filtered[column]=null;previous[column]=null;return;
   }
   if(previous[column]==null||dt<=0||dt>lastStep*1.5)filtered[column]=value;
   else filtered[column]+=dt/(smoothingSeconds+dt)*(value-filtered[column]);
   copy[column]=filtered[column];previous[column]=utc;
  });
  lastStep=row[stepColumn]||60;
  return copy;
 });
}
function fanBurnEnvelope(points){
 const result=points.map(row=>[...row]);
 const active=row=>[1,2,3].includes(row[4])&&typeof row[3]==='number'&&Number.isFinite(row[3]);
 let start=0;
 while(start<result.length){
  if(!active(result[start])){start++;continue}
  let end=start+1;
  while(end<result.length&&active(result[end])&&result[end][0]>result[end-1][0]&&
        result[end][0]-result[end-1][0]<=(result[end-1][5]||60)*1.5)end++;
  const anchors=[];
  for(let i=start;i<end;i++)if(result[i][3]>0)anchors.push(i);
  let next=0,filtered=null;
  for(let i=start;i<end;i++){
   const row=result[i],minimum=row[6],maximum=row[7];
   while(next<anchors.length&&anchors[next]<i)next++;
   const a=anchors[Math.max(0,next-1)],b=anchors[Math.min(next,anchors.length-1)];
   let value=null;
   if(anchors.length){
    value=a===b?points[a][3]:points[a][3]+(points[b][3]-points[a][3])*
     Math.max(0,Math.min(1,(row[0]-points[a][0])/(points[b][0]-points[a][0])));
   }else if(typeof minimum==='number'&&Number.isFinite(minimum))value=minimum;
   if(value!==null){
    if(filtered!==null){const dt=row[0]-result[i-1][0];value=filtered+dt/(60+dt)*(value-filtered)}
    if(typeof minimum==='number'&&Number.isFinite(minimum))value=Math.max(minimum,value);
    if(typeof maximum==='number'&&Number.isFinite(maximum)&&maximum>=minimum)value=Math.min(maximum,value);
   }
   row[3]=value;filtered=value;
  }
  start=end;
 }
 return result;
}
function smoothChartExhaust(points,sampleSeconds,live=[]){
 // Q rows predate the cadence change and don't store an interval. Keep legacy W/P at 60 seconds.
 const timed=sampleSeconds===30?points.map(row=>row[5]===15?[...row.slice(0,5),sampleSeconds,...row.slice(6)]:row):points;
 const water=smoothChartReadings(timed.concat(live),[1],5,1,90);
 const filtered=smoothChartReadings(water,[2],5,1,240);
 return fanBurnEnvelope(filtered.slice(0,points.length)).concat(filtered.slice(points.length));
}
function chartCurve(ctx,previous,px,py){
 if(!previous){ctx.moveTo(px,py);ctx.lineTo(px,py)}
 else{
  const middle=(previous[0]+px)/2;
  ctx.bezierCurveTo(middle,previous[1],middle,py,px,py);
 }
 return [px,py];
}
function drawDamperOpen(ctx,points,start,end,x,top,plotH){
 const py=top+plotH/2;
 ctx.save();ctx.strokeStyle='#c3a6ff';ctx.lineWidth=2;ctx.lineCap='butt';
 ctx.beginPath();
 let lastEnd=null;
 for(let i=1;i<points.length;i++){
  const [utc,,,,,step,,,command]=points[i-1],next=points[i];
  if(command!==1||(next[8]!==0&&next[8]!==1)||next[0]-utc>(step||15)*1.5)continue;
  const a=Math.max(start,utc),b=Math.min(end,next[0]);
  if(b<=a)continue;
  if(a!==lastEnd)ctx.moveTo(x(a),py);
  ctx.lineTo(x(b),py);lastEnd=b;
 }
 ctx.stroke();
 ctx.restore();
}
function drawSdWaterChart(){
 renderLinkedHeatChart();
 const width=historyViewport.clientWidth,height=historyViewport.clientHeight;
 if(!width||!height)return;
 const scale=devicePixelRatio||1;
 historyCanvas.width=Math.round(width*scale);historyCanvas.height=Math.round(height*scale);
 historyCanvas.style.height=height+'px';
 const ctx=historyCanvas.getContext('2d');ctx.scale(scale,scale);
 const saved=historySnapshot?.available;
 const end=saved?historyViewEndUtc:latestTrendTime(),start=end-historyViewSeconds;
 const recorded=saved?historySnapshot.water:[];
 const live=liveTrend.filter(row=>row[0]>(recorded.at(-1)?.[0]||0))
  .map(([utc,tank,exhaust,fan])=>[utc,tank,exhaust,fan,null,10]);
 const filtered=smoothChartExhaust(recorded,historySnapshot?.sample_seconds,live);
 const points=filtered.slice(0,recorded.length),tail=filtered.slice(recorded.length);
 const ticks=width<600?2:4;
 const left=historyPlotLeft,right=historyPlotRight,top=20,bottom=32,plotW=width-left-right,plotH=height-top-bottom;
 const x=historyPlotX(width,start);
 const y=(v,low,span)=>top+plotH-(v-low)/span*plotH;
 const axes=[{column:2,color:'#ff6b68',low:0,span:900},
  {column:1,color:'#4ba3ff',low:120,span:80},{column:3,color:'#f6c65b',low:0,span:100}];
 shadeActiveBurns(ctx,points.filter(p=>p.length>2).map(p=>[p[0],p[4],p[5]]),start,end,x,top,plotH);
 ctx.font='10px sans-serif';ctx.strokeStyle='rgba(160,185,200,.22)';
 for(let i=0;i<=ticks;i++){
  const fraction=i/ticks,py=y(fraction,0,1);
  ctx.beginPath();ctx.moveTo(left,py);ctx.lineTo(width-right,py);ctx.stroke();
  ctx.textAlign='right';
  axes.forEach(({color,low,span},j)=>{
   ctx.fillStyle=color;ctx.fillText(String(low+span*fraction),width-47+j*22,py+3);
  });
 }
 drawHistoryTimeAxis(ctx,start,end,ticks,x,height);
 ctx.save();ctx.beginPath();ctx.rect(left,top,plotW,plotH);ctx.clip();
 let shown=0;
 axes.forEach(series=>{
  ctx.strokeStyle=series.color;ctx.lineWidth=series.column===3?1.8:2;ctx.lineJoin='round';ctx.lineCap='round';
  ctx.beginPath();let previous=null,previousStep=60,curve=null;
  points.forEach(row=>{
   const utc=row[0],value=row[series.column];
   const gapLimit=series.column===1?120:previousStep*1.5;
   if(utc<start-120||utc>end+120)return;
   if(typeof value!=='number'||!Number.isFinite(value)){
    if(series.column!==1){previous=null;curve=null}
    return;
   }
   if(previous===null||utc-previous>gapLimit||utc<=previous)curve=null;
   curve=chartCurve(ctx,curve,x(utc),y(value,series.low,series.span));
   if(utc>=start&&utc<=end)shown++;
   previous=utc;
   previousStep=row[5]||60;
  });
  ctx.stroke();ctx.setLineDash([3,3]);ctx.beginPath();let liveStarted=false;
  if(series.column===3)curve=null;
  for(const row of tail){
   const utc=row[0];
   if(utc<start||utc>end)continue;
   const value=row[series.column];
   if(!Number.isFinite(value)){curve=null;continue}
   if(previous===null||utc-previous>previousStep*1.5)curve=null;
   const px=x(utc),py=y(value,series.low,series.span);
   if(!liveStarted&&curve)ctx.moveTo(...curve);
   liveStarted=true;
   curve=chartCurve(ctx,curve,px,py);previous=utc;previousStep=series.column===2?5:row[5];shown++;
   ctx.fillStyle=series.color;ctx.fillRect(px-1,py-1,2,2);
  }
  ctx.stroke();
  if(series.column===2){
   ctx.setLineDash([]);ctx.strokeStyle='#b98180';ctx.beginPath();
   let last=null,step=60,missing=0;
   const bridge=(row,isLive)=>{
    if(row[0]<start-95||row[0]>end+95)return;
    if(!Number.isFinite(row[2])){missing++;return}
    if(last){
     const dt=row[0]-last[0];
     if(dt>0&&missing<=2&&dt<=Math.min(95,step*3+(step===5?2:5))&&
        (missing||dt>step*1.5)){
      ctx.moveTo(x(last[0]),y(last[2],series.low,series.span));
      ctx.lineTo(x(row[0]),y(row[2],series.low,series.span));
     }
    }
    last=row;step=isLive?5:row[5]||60;missing=0;
   };
   points.forEach(row=>bridge(row,false));
   tail.forEach(row=>bridge(row,true));
   ctx.stroke();
  }
  ctx.setLineDash([]);
 });
 drawDamperOpen(ctx,points,start,end,x,top,plotH);
 ctx.restore();
 if(!shown){ctx.textAlign='center';ctx.fillStyle='#bdcdd5';ctx.fillText(saved?'No recorded data in this window':'SD history unavailable',width/2,height/2)}
}
function renderSdBurnTimes(){
 const root=document.getElementById('burnHistory');root.replaceChildren();
 if(!historySnapshot.available)return;
 if(!historySnapshot.burns.length){
  if(historyLoadedDepth>=BURN_HISTORY_SECONDS&&!historyLoading&&!historyLoadError&&!historyBusyRetryAt)
   root.textContent='No completed periods recorded in the last 24 hours.';
  return;
 }
 const burns=historySnapshot.burns;
 const complete=historyLoadedDepth>=BURN_HISTORY_SECONDS&&!historyLoading&&!historyLoadError&&!historyBusyRetryAt;
 burns.forEach((b,i)=>{
  const start=b.end_utc-b.duration_sec,previous=burns[i+1];
  const gap=previous?start-previous.end_utc:null;
  const row=document.createElement('div');row.className='historyRow';
  const index=document.createElement('div');index.className='index';index.textContent=i+1;row.appendChild(index);
  const cells=[
   ['Started / ended',historyLocalTime(start,true)+' / '+historyLocalTime(b.end_utc,true)],
   ['Burn time',formatDuration(b.duration_sec)],
   ['Between burns',gap===null?(complete?'No earlier burn in view':'Earlier burn not loaded'):
    gap<0?'SD times overlap':gap===0?'0s':formatDuration(gap)]
  ];
  cells.forEach(([label,value])=>{
   const cell=document.createElement('div'),heading=document.createElement('span'),text=document.createElement('span');
   heading.className='historyLabel';heading.textContent=label;
   text.className='historyValue';text.textContent=value;cell.append(heading,text);row.appendChild(cell);
  });
  root.appendChild(row);
 });
}
historyViewport.addEventListener('scroll',()=>{
 if(!historySnapshot?.available||!activeTab('state'))return;
 const max=historyViewport.scrollWidth-historyViewport.clientWidth;
 const fraction=max>0?historyViewport.scrollLeft/max:1;
 historyScroller.value=Math.round(fraction*10000);
 historyScroller.disabled=max<=0;
 historyFollowLatest=max-historyViewport.scrollLeft<2;
 historyViewEndUtc=latestTrendTime()-HISTORY_SECONDS+historyViewSeconds+
  fraction*(HISTORY_SECONDS-historyViewSeconds);
 if(historyScrollFrame!==null)cancelAnimationFrame(historyScrollFrame);
 historyScrollFrame=requestAnimationFrame(()=>{historyScrollFrame=null;drawSdWaterChart()});
},{passive:true});
enableHistoryDrag(historyViewport);
window.addEventListener('resize',()=>{if(activeTab('state'))positionHistoryChart()});
</script>
)historyhtml"
