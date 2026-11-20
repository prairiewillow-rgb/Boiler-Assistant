/*
 * Boiler Assistant - SD Log Dashboard (v3.4.0 "Total Domination")
 * Adjacent string literal included by DashboardHTML.h.
 * After editing any dashboard file, run tools\make_dashboard_gz.ps1.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
R"sdloghtml(
<style>
#sdLog .card{margin-bottom:14px}
#sdLog table{width:100%;border-collapse:collapse;font-size:.85rem}
#sdLog th,#sdLog td{padding:8px;text-align:left;border-bottom:1px solid var(--line)}
#sdLog .sdFiles{overflow-x:auto}
#sdLog a{color:var(--cyan)}
</style>
<script>
let sdFetchedAt=0;
const sdBefore={LOGS:''};
const sdSection=document.createElement('div');
sdSection.id='sdLog';
sdSection.hidden=true;sdSection.setAttribute('role','tabpanel');
sdSection.innerHTML=`<h2>SD log</h2>
<div class="card"><h2>Status</h2><div id="sdStatus" class="sdFiles" role="status">Loading...</div></div>
<div class="card"><h2>Daily log</h2><p class="hint">One file per UTC day: readings, events, chart history and burn summaries. Startup entries wait in RAM for network time.</p><div id="sdLOGS" class="sdFiles">-</div>
<div class="actions"><button data-sd-more="LOGS">Older files</button><button data-sd-reset="LOGS">Newest files</button></div></div>
<p class="hint">When space runs low, older unified logs may be deleted. Existing files in the old folders are left alone.</p>`;
document.getElementById('settings').appendChild(sdSection);
function sdLogVisible(){return activeTab('settings')&&!sdSection.hidden}
function sdSize(bytes){
 if(typeof bytes!=='number')return 'Unknown';
 return bytes>=1048576?(bytes/1048576).toFixed(1)+' MB':bytes>=1024?(bytes/1024).toFixed(1)+' KB':bytes+' B';
}
function sdRows(rootId,headers,rows){
 const root=document.getElementById(rootId);root.replaceChildren();
 const table=document.createElement('table');const hr=document.createElement('tr');
 headers.forEach(h=>{const c=document.createElement('th');c.textContent=h;hr.appendChild(c)});
 table.appendChild(hr);
 rows.forEach(values=>{const r=document.createElement('tr');values.forEach(v=>{const c=document.createElement('td');
  if(v instanceof Node)c.appendChild(v);else c.textContent=v;r.appendChild(c)});table.appendChild(r)});
 root.appendChild(table);
}
function renderSdStatus(s){
 const yes=v=>v?'Yes':'No';
 const rows=[['State',s.state||'Unknown']];
 if(s.enabled!==false){
  rows.push(['Clock set',yes(s.clock_valid)],['Card size',typeof s.card_mb==='number'?s.card_mb+' MB':'Unknown'],
   ['Free space',typeof s.free_mb==='number'?s.free_mb+' MB':'Unknown until the card reports it'],
   ['Log files total',typeof s.log_mb==='number'?s.log_mb.toFixed(1)+' MB':'Unknown'],['Current data file',s.file||'None'],
   ['Rows written / waiting / lost',(s.rows_logged??0)+' / '+(s.rows_pending??0)+' / '+(s.rows_lost??0)],
   ['Events written / lost',(s.events_logged??0)+' / '+(s.events_lost??0)],
   ['Burn summaries not saved',String(s.burns_lost??0)],
   ['History rows not saved',String(s.history_rows_lost??0)],
   ['History/events pending',(s.history_rows_pending??0)+' / '+(s.events_pending??0)],
   ['Logging suspended',yes(s.logging_suspended)],
   ['RAM buffers',sdSize(s.buffer_bytes)],
   ['SD/read max',(s.max_sd_step_ms??0)+' / '+(s.max_download_read_ms??0)+' ms'],
   ['Old days deleted',String(s.files_deleted??0)],
   ['Batch last/max',(s.last_write_ms??0)+' / '+(s.max_write_ms??0)+' ms'],
   ['Last error',s.last_error||'None']);
 }
 sdRows('sdStatus',['Item','Value'],rows);
}
async function loadSdList(dir){
 const root='sd'+dir;
 try{
  const q='/api/sd/list?d='+dir+(sdBefore[dir]?'&before='+encodeURIComponent(sdBefore[dir]):'');
  const data=await requestJson(q);
  if(data.error){text(root,data.error);return null}
  const files=Array.isArray(data.files)?data.files:[];
  if(!files.length){text(root,'No files.');return data}
  sdRows(root,['File','Size',''],files.map(f=>{
   const a=document.createElement('a');a.href='/api/sd/file?f='+dir+'/'+encodeURIComponent(f[0]);
   a.textContent='Download';a.setAttribute('download',f[0]);
   a.onclick=()=>text('sdStatus','Download started: '+f[0]+'. Live updates pause until it finishes.');
   return [f[0],sdSize(f[1]),a];
  }));
  return data;
 }catch(error){text(root,'Unavailable - list request failed');throw error}
}
document.querySelectorAll('[data-sd-more]').forEach(b=>b.onclick=async()=>{
 const dir=b.dataset.sdMore;const rows=document.querySelectorAll('#sd'+dir+' td:first-child');
 if(!rows.length)return;sdBefore[dir]=rows[rows.length-1].textContent;
 const data=await loadSdList(dir).catch(()=>null);
 if(data&&!data.more)b.disabled=true;
});
document.querySelectorAll('[data-sd-reset]').forEach(b=>b.onclick=()=>{
 const dir=b.dataset.sdReset;sdBefore[dir]='';
 const more=document.querySelector('[data-sd-more="'+dir+'"]');if(more)more.disabled=false;
 loadSdList(dir).catch(()=>{});
});
async function refreshSdLog(){
 if(!sdLogVisible()||document.hidden||Date.now()-sdFetchedAt<15000)return;
 sdFetchedAt=Date.now();
 const s=await requestJson('/api/sd');
 renderSdStatus(s);
 if(typeof s.card_mb==='number'){
  if(!sdBefore.LOGS)await loadSdList('LOGS');
 }else text('sdLOGS','No card ready.');
}
</script>
)sdloghtml"
