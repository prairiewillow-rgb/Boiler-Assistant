/*
 * "What's new" card. CC BY-NC-SA 4.0.
 * Update these notes for each release; Version.h supplies the version.
 */
R"whatsnew(
<style>#whatsNew{margin:14px 0}#whatsNew h2{margin-top:0}</style>
<script>{
const version=')whatsnew" FW_VERSION R"whatsnew(',key='boiler-seen-version',card=document.createElement('div');
card.id='whatsNew';card.className='card';
card.innerHTML='<h2>What\'s new in '+version+'</h2><ul><li>SD logging and 24-hour charts, with recent readings loading first.</li><li>Adjustable damper delay, BOOST speed and time, including seasonal profiles.</li><li>Recorded loop temperatures and water-probe calibration.</li><li>Fixed IP, improved Wi-Fi recovery and saved-settings diagnostic reports.</li></ul><button type="button">Got it</button><div class="hint" role="status"></div>';
const button=card.querySelector('button'),status=card.querySelector('.hint');
function storageError(error){console.warn("What's new preference:",error);status.textContent='Cannot remember this choice in this browser. The card may appear again.'}
function dismiss(){button.hidden=true;document.getElementById('settings').append(card)}
button.onclick=()=>{try{localStorage.setItem(key,version)}catch(error){storageError(error)}dismiss()};
let seen=false;
try{seen=localStorage.getItem(key)===version}catch(error){storageError(error)}
if(seen)dismiss();else document.getElementById('state').prepend(card);
}</script>
)whatsnew"
