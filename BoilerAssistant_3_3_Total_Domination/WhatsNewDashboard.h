/*
 * "What's new" card; adjacent dashboard literal. CC BY-NC-SA 4.0
 * Shown at the top of Main once per browser after an update, then kept
 * at the bottom of Settings. Update the list for every release.
 */
R"whatsnew(<script>{const v=')whatsnew" FW_VERSION R"whatsnew(',k='boiler-seen-version',c=document.createElement('div');c.className='card';
c.innerHTML='<h2>What\'s new in '+v+'</h2><b>New</b><ul><li>Screen turns off after 5 min with no key press; stays on for alarms. First key press only wakes it.</li></ul><b>Fixed</b><ul><li>A frozen screen is reset when woken, or on a key press after a pause.</li><li>Wi-Fi is more patient on weak signal, so opening the app is less likely to drop it.</li><li>Continuous mode works like 3.3.9 again: no ZC wait, starts at power-up, ignores tank temp.</li></ul><button>Got it</button>';
const b=c.querySelector('button'),done=()=>{try{localStorage.setItem(k,v)}catch(e){}b.hidden=true;document.getElementById('settings').append(c)};
let s;try{s=localStorage.getItem(k)}catch(e){}
if(s===v)done();else{document.getElementById('state').prepend(c);b.onclick=done}}</script>)whatsnew"