/*
 * Boiler Assistant - Live and recorded water probe temperatures
 * Adjacent string literal included by DashboardHTML.h.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
R"performance(
<style>
#performance canvas{width:100%;height:100%;display:block}
#performanceViewport{height:220px;overflow:hidden;border:1px solid transparent}
#performanceHeat{position:sticky;left:0;min-width:0}
#performance .legend{display:flex;flex-wrap:wrap;gap:12px;margin-top:8px;font-size:.85rem}
#performance .legend span{border-left:4px solid;padding-left:7px}
</style>
<script>
let performanceSnapshot=null;
const performanceSection=document.createElement('div');
performanceSection.id='performance';
performanceSection.innerHTML=`<h2>Loop temperatures</h2>
<div id="performanceStatus" role="status">Insert a working SD card to view graphs.</div>
<div><div id="performanceViewport"><canvas id="performanceHeat" role="img" aria-label="Loop temperatures in Fahrenheit; dashed lines are live"></canvas></div>
<div id="performanceTimeRange" hidden></div>
<div class="legend" id="loopLegend"></div>
</div>`;
function visiblePerformanceSamples(){
 const now=performanceSnapshot.now_utc,offset=now-(historyViewEndUtc??now);
 const saved=performanceSnapshot.samples;
 const last=saved.at(-1)?.[0]??Infinity;
 return saved.concat(liveTrend.filter(row=>now-row[0]<last)
  .map(row=>[now-row[0],row[4],true])).filter(s=>s[0]>=offset&&s[0]<=offset+historyViewSeconds);
}
const loopGap=previous=>previous?.[2]?15:120;
function smoothPerformanceSamples(samples,series){
 const smoothed=new Map();
 for(const item of series){
  let filtered=null,previous=null;
  const values=new Map();
  for(const row of samples){
   const value=row[1][item.id],age=row[0];
   if(typeof value!=='number'||!Number.isFinite(value)){
    filtered=null;previous=null;continue;
   }
   const elapsed=previous?.[0]-age;
   if(!previous||elapsed<=0||elapsed>loopGap(previous))filtered=value;
   else filtered+=elapsed/(60+elapsed)*(value-filtered);
   values.set(row,filtered);previous=row;
  }
  smoothed.set(item.id,values);
 }
 return smoothed;
}
function performanceChart(series,samples){
 const canvas=document.getElementById('performanceHeat');
 const width=canvas.clientWidth,height=historyViewport.clientHeight,scale=devicePixelRatio||1;
 if(!width||!height)return;
 canvas.width=Math.round(width*scale);canvas.height=Math.round(height*scale);
 const ctx=canvas.getContext('2d');ctx.scale(scale,scale);ctx.clearRect(0,0,width,height);
 ctx.font='11px sans-serif';ctx.fillStyle='#bdcdd5';
 const compact=width<600,ticks=compact?2:4;
 const left=historyPlotLeft,right=historyPlotRight,top=20,bottom=32,w=width-left-right,h=height-top-bottom;
 const valid=v=>typeof v==='number'&&Number.isFinite(v);
 const smoothed=smoothPerformanceSamples(samples,series);
 const low=120,span=80;
 const y=v=>top+h-(v-low)/span*h;
 const now=performanceSnapshot?.now_utc||0,end=historyViewEndUtc??now,start=end-historyViewSeconds;
 const x=historyPlotX(width,start);
 ctx.font='11px sans-serif';ctx.fillStyle='#bdcdd5';ctx.strokeStyle='rgba(160,185,200,.22)';
 for(let i=0;i<=ticks;i++){
  const value=low+span*i/ticks,py=y(value);
  ctx.beginPath();ctx.moveTo(left,py);ctx.lineTo(width-right,py);ctx.stroke();
  ctx.textAlign='right';
  ctx.fillText(String(value),width-4,py+3);
 }
 ctx.fillText('°F',width-4,12);
 drawHistoryTimeAxis(ctx,start,end,ticks,x,height);
 ctx.save();ctx.beginPath();ctx.rect(left,top,w,h);ctx.clip();
 series.forEach(s=>{
  ctx.strokeStyle=s.color;ctx.lineWidth=2;ctx.lineJoin='round';ctx.lineCap='round';ctx.beginPath();let curve=null,previous=null;
  let live=false;
  samples.forEach(row=>{
   if(row[2]&&!live){
    ctx.stroke();ctx.setLineDash([3,3]);ctx.beginPath();
    if(curve)ctx.moveTo(...curve);
    live=true;
   }
   const value=smoothed.get(s.id).get(row);
   if(!valid(value)){curve=null;previous=null;return}
   const px=x(now-row[0]),py=y(value);
   if(!previous||previous[0]-row[0]>loopGap(previous)||previous[0]<=row[0])curve=null;
   curve=chartCurve(ctx,curve,px,py);
   previous=row;
  });ctx.stroke();ctx.setLineDash([]);
 });
 ctx.restore();
 if(!series.some(s=>smoothed.get(s.id).size)){
  ctx.textAlign='center';ctx.fillText('No recorded loop temperatures in this window',width/2,height/2);
 }
}
function loopProbeStyle(id){
 const probe=lastState?.water?.find(p=>String(p.id).toUpperCase()===id),name=probe?.name?.trim()||'Probe';
 const tankPosition=/^(?:main\s+)?tank\s+(top|bottom)$/i.exec(name);
 if(tankPosition)return {color:tankPosition[1].toLowerCase()==='top'?'#368de5':'#a6d2ff',name};
 const match=/^(?:loop\s*|l)([1-4])\s+(supply|return)$/i.exec(name);
 const colors=[['#3aaa6c','#a0e8bd'],['#ad70dc','#dfbafa'],['#e28442','#ffd0a6'],['#d9534f','#ffaaa8']];
 if(match)return {color:colors[Number(match[1])-1][match[2].toLowerCase()==='return'?1:0],name};
 if(probe?.controls_tank||/^(?:(?:main|other)\s+)?tank(?:\s+.*)?$/i.test(name))return {color:'#f6c65b',name};
 return {color:'#bdcdd5',name};
}
function loopProbeOrder(id){
 const probe=lastState?.water?.find(p=>String(p.id).toUpperCase()===id),name=probe?.name?.trim()||'Probe';
 const tank=/^(?:(?:main|other)\s+)?tank(?:\s+(top|bottom))?(?:\s+.*)?$/i.exec(name);
 if(tank)return tank[1]?.toLowerCase()==='bottom'?1:tank[1]?.toLowerCase()==='top'?0:2;
 const loop=/^(?:loop\s*|l)([1-4])\s+(supply|return)$/i.exec(name);
 if(loop)return 3+(Number(loop[1])-1)*2+(loop[2].toLowerCase()==='return'?1:0);
 return 20;
}
function renderPerformance(data){
 performanceSnapshot=data;
 const local=t=>historyLocalTime(t,true,data.utc_offset_minutes||0);
 text('performanceTimeRange',data.available?
  local(historyViewEndUtc-historyViewSeconds)+' - '+local(historyViewEndUtc)+' (controller time)':'');
 const samples=visiblePerformanceSamples();
 text('performanceStatus',!data.available?'SD history unavailable; showing live readings when received.':
  !samples.length?'No recorded loop temperatures in this range. Older history has no probe-temperature records.':
  'Dashed lines are live. '+
   (data.invalid_rows?' '+data.invalid_rows+' damaged/incomplete rows skipped.':''));
 document.getElementById('performanceStatus').hidden=!!data.available&&samples.length>0&&!data.invalid_rows;
 const ids=[...new Set(samples.flatMap(row=>Object.keys(row[1])))].sort((a,b)=>
  loopProbeOrder(a)-loopProbeOrder(b)||a.localeCompare(b));
 const series=ids.map(id=>({id,...loopProbeStyle(id)}));
 const legend=document.getElementById('loopLegend');
 legend.replaceChildren();
 for(const item of series){
  const label=document.createElement('span');
  label.style.borderColor=item.color;
  label.textContent=item.name;
  legend.appendChild(label);
 }
 performanceChart(series,samples);
}
</script>
)performance"
