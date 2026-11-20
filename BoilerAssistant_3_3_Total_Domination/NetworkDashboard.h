/*
 * Network settings alongside notifications. CC BY-NC-SA 4.0.
 */
R"networkhtml(
<style>#networkFields{border:0;padding:0;margin:0;min-width:0}</style>
<script>
const networkEditor=document.createElement('div');
networkEditor.innerHTML=`<h2>Wi-Fi and IP</h2>
<p class="hint" id="networkCurrent">Load saved network settings to edit.</p>
<fieldset id="networkFields" disabled><div class="form">
<div class="field"><label for="networkSsid">Wi-Fi name (SSID)</label><input id="networkSsid" maxlength="31" autocomplete="off"></div>
<div class="field"><label for="networkPass">Wi-Fi password (blank keeps current)</label><input id="networkPass" type="password" maxlength="63" autocomplete="new-password"></div>
<div class="field"><label for="networkMode">Address</label><select id="networkMode"><option value="dhcp">Automatic (DHCP)</option><option value="fixed">Fixed IP</option></select></div>
</div><div class="form" id="networkFixed" hidden>
<div class="field"><label for="networkIp">IP address</label><input id="networkIp" maxlength="15" inputmode="decimal"></div>
<div class="field"><label for="networkGateway">Gateway</label><input id="networkGateway" maxlength="15" inputmode="decimal"></div>
<div class="field"><label for="networkSubnet">Subnet</label><input id="networkSubnet" maxlength="15" inputmode="decimal"></div>
<div class="field"><label for="networkDns">DNS</label><input id="networkDns" maxlength="15" inputmode="decimal"></div>
</div><p class="hint">Changing Wi-Fi name requires its password. Use a reserved address outside the DHCP pool for Fixed IP.</p>
<button id="networkSave" type="button" class="primary">Save and reconnect Wi-Fi</button></fieldset>
<div class="actions"><button id="networkLoad" type="button">Load network settings</button><button id="networkReset" type="button" class="danger">Reset network</button></div>
<p id="networkMessage" role="status"></p><p class="hint">Reconnects only Wi-Fi; the controller does not restart. Reset clears Wi-Fi and Fixed IP only. Boiler settings, notifications and the control password stay saved.</p>`;
notificationSettings.prepend(networkEditor);
let networkBusy=false,networkLoaded=false;
const networkIds=['networkSsid','networkPass','networkMode','networkIp','networkGateway','networkSubnet','networkDns'];
function networkModeChanged(){document.getElementById('networkFixed').hidden=document.getElementById('networkMode').value!=='fixed'}
document.getElementById('networkMode').onchange=networkModeChanged;
function networkLock(busy){
 networkBusy=busy;document.getElementById('networkFields').disabled=busy||!networkLoaded;
 for(const id of ['networkLoad','networkReset'])document.getElementById(id).disabled=busy;
}
async function loadNetwork(){
 if(networkBusy||!ensureToken())return;
 networkLock(true);text('networkMessage','Loading...');
 try{
  const saved=await requestJson('/api/network',{headers:{'X-Boiler-Token':token()}});
  const values=[saved.ssid,'',saved.addressMode,saved.ip,saved.gateway,saved.subnet,saved.dns];
  networkIds.forEach((id,i)=>set(id,values[i]));
  networkModeChanged();networkLoaded=true;
  text('networkCurrent','Current IP: '+saved.current_ip);text('networkMessage','Network settings loaded.');
 }catch(error){networkLoaded=false;text('networkMessage',error.message);throw error}
 finally{networkLock(false)}
}
async function changeNetwork(reset){
 if(networkBusy||(!reset&&!networkLoaded)||!ensureToken())return;
 const mode=document.getElementById('networkMode').value,ip=document.getElementById('networkIp').value;
 if(!confirm(reset?'Clear saved Wi-Fi and Fixed IP, then open BoilerAssistant-Setup? Boiler control continues; this page will disconnect.':'Save network settings and reconnect Wi-Fi? Boiler control continues; this page will disconnect.'))return;
 const body=reset?'':new URLSearchParams(Object.fromEntries(['ssid','pass','addressMode','ip','gateway','subnet','dns'].map((key,i)=>[key,document.getElementById(networkIds[i]).value]))).toString();
 networkLock(true);text('networkMessage','Saving...');
 try{
  const result=await requestJson('/api/network'+(reset?'/reset':''),{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Boiler-Token':token()},body});
  if(!result.ok)throw new Error(result.error||'Network change rejected');
  document.getElementById('networkPass').value='';
  text('networkMessage',reset?'Saved. Join BoilerAssistant-Setup, then open http://192.168.4.1.':mode==='fixed'?'Saved. Reconnecting; open http://'+ip+'.':'Saved. Reconnecting; check the IP on the keypad, then reopen the dashboard.');
 }catch(error){networkLock(false);text('networkMessage',error.message+' If the connection dropped, check the selected network or setup hotspot before retrying.');throw error}
}
document.getElementById('networkLoad').onclick=runAction(loadNetwork);
document.getElementById('networkSave').onclick=runAction(()=>changeNetwork(false));
document.getElementById('networkReset').onclick=runAction(()=>changeNetwork(true));
</script>
)networkhtml"
