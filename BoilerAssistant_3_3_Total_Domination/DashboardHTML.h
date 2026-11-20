/*
 * Boiler Assistant - DashboardHTML
 * Maintainer: Karl (Embedded Systems Architect)
 * License: CC BY-NC-SA 4.0
 */

#ifndef DASHBOARD_HTML_H
#define DASHBOARD_HTML_H

#include <Arduino.h>
#include "Version.h"

// Live polling waits 5s after a completed cycle. Hidden tabs stop polling;
// all API calls share one queue through JSON consumption (15s timeout).
// Settings and history are cached for 60s; local animations do not use HTTP.
// Served from DashboardGz.h (gzip). This source copy is only checked at compile
// time and is not linked; run tools\make_dashboard_gz.ps1 after editing.
constexpr char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Boiler Assistant</title>
<style>
:root{color-scheme:dark;--bg:#25323b;--panel:#33444e;--card:#394c57;--line:#5a707d;--text:#f2f6f7;--muted:#bdcdd5;--hot:#f6a623;--cyan:#75d8da;--cold:#4ba3ff;--good:#8edaa6;--danger:#ef625d;--shadow:0 8px 24px #17212955}
*{box-sizing:border-box}body{margin:0;padding:22px;background:radial-gradient(circle at 85% 0,#3d5a5e 0,var(--bg) 60%);color:var(--text);font:15px "Trebuchet MS",Segoe UI,sans-serif;min-height:100vh}.wrap{max-width:1080px;margin:auto}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:10px;padding:16px 22px;background:var(--panel);border:1px solid var(--line);border-radius:12px;box-shadow:var(--shadow)}.brand{text-align:center}.brandMark{position:relative;width:58px;height:52px;margin:0 auto 7px;border:3px solid var(--hot);border-radius:11px 11px 15px 15px;background:#25343a;box-shadow:inset 0 -8px 0 #152126}.brandMark:before{content:"";position:absolute;left:12px;right:12px;top:-9px;height:8px;border:3px solid var(--hot);border-bottom:0;border-radius:6px 6px 0 0}.brandDoor{position:absolute;left:17px;bottom:4px;width:19px;height:22px;border:2px solid #68767a;border-radius:9px 9px 3px 3px;background:#101418}.brandFire{position:absolute;left:23px;bottom:9px;width:9px;height:14px;border-radius:65% 35% 65% 35%;background:var(--hot);transform:rotate(25deg);box-shadow:0 0 9px #f6a62399;opacity:0}.brandMark.burning .brandFire{opacity:1;animation:brandFire .8s ease-in-out infinite alternate}.brandMark.idle .brandFire{opacity:.45}.brandSmoke{position:absolute;left:21px;top:-28px;width:16px;height:25px;opacity:0}.brandSmoke span{position:absolute;width:8px;height:8px;border:2px solid var(--muted);border-radius:50%;opacity:0}.brandMark.burning .brandSmoke{opacity:1}.brandMark.burning .brandSmoke span{animation:brandSmoke 2.2s ease-out infinite}.brandSmoke span:nth-child(1){left:7px;bottom:0}.brandSmoke span:nth-child(2){left:0;bottom:8px;animation-delay:.7s}.brandSmoke span:nth-child(3){left:8px;bottom:16px;animation-delay:1.4s}.brandZzz{position:absolute;right:-22px;top:2px;color:var(--cyan);font-size:.7rem;font-weight:700;opacity:0}.brandMark.idle .brandZzz{opacity:1;animation:brandZzz 2s ease-in-out infinite}.brandName{color:var(--hot);font-size:1.18rem;font-weight:700;letter-spacing:.03em;white-space:nowrap}.brandTemp{color:var(--muted);font-size:.68rem;margin-top:2px;white-space:nowrap}.status{color:var(--cyan);font-size:.9rem;white-space:nowrap}.status:before{content:"";display:inline-block;width:8px;height:8px;margin-right:7px;border-radius:50%;background:var(--good);box-shadow:0 0 10px var(--good)}
nav{display:flex;gap:8px;margin-top:18px;border-top:1px solid var(--line);padding-top:12px}button{border:1px solid var(--line);border-radius:7px;padding:10px 14px;background:#243139;color:var(--text);cursor:pointer;font:inherit;transition:.18s ease}button:hover{border-color:var(--cyan);transform:translateY(-1px)}button.active,button.primary{background:var(--hot);color:#101418;border-color:var(--hot);font-weight:700}button.danger{background:var(--danger);border-color:var(--danger);color:#111}
section{display:none}section.active{display:block}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(185px,1fr));gap:13px}.card{padding:17px;background:linear-gradient(145deg,#213039,#182228);border:1px solid var(--line);border-radius:10px;box-shadow:0 8px 18px #080b0d45}.label{color:var(--muted);font-size:.78rem;text-transform:uppercase;letter-spacing:.08em;margin-bottom:8px}.value{font-size:1.55rem;font-weight:700}.envStrip{display:grid;grid-template-columns:repeat(3,1fr);gap:7px;margin-bottom:14px}.envItem{min-width:0;padding:8px 9px;background:#172228;border:1px solid var(--line);border-radius:7px}.envItem .label{font-size:.62rem;margin-bottom:3px;white-space:nowrap}.envItem .value{font-size:1rem;white-space:nowrap;color:var(--cyan)}.hero{display:grid;grid-template-columns:1.2fr 1fr;gap:14px;margin-bottom:14px}.hero,.hero .card{min-height:190px}.hero .card{display:flex;flex-direction:column;justify-content:space-between}.hero> .card:first-child{text-align:center}.stateValue{font-size:1.85rem;color:var(--hot);line-height:1;text-align:center}.stateMeta{color:var(--muted);font-size:.9rem}.gaugeRow{display:grid;grid-template-columns:1fr 1fr;gap:18px}.gauge{position:relative;display:grid;place-items:center;width:126px;height:126px;margin:auto;border-radius:50%;background:conic-gradient(var(--gauge-color,var(--cyan)) var(--gauge,0%),#30414a 0)}.gauge:after{content:"";position:absolute;inset:10px;border-radius:50%;background:#182127}.gaugeValue{position:relative;z-index:1;font-size:1.45rem;font-weight:700;text-align:center}.gaugeLabel{text-align:center;color:var(--muted);font-size:.78rem;text-transform:uppercase;letter-spacing:.08em;margin-top:8px}.fanDisplay{text-align:center}.fanGauge{background:radial-gradient(circle,#182127 0 56%,#30414a 57% 59%,#182127 60%);border:4px solid #68767a;box-shadow:inset 0 0 0 2px #101418,0 5px 10px #080b0d80}.fanGauge:after{inset:8px;background:repeating-radial-gradient(circle,#0000 0 15px,#68767a 16px 17px)}.fanPercent{margin-top:7px;font-size:1.25rem;font-weight:700;color:var(--cyan)}.fanIcon{position:absolute;z-index:1;width:55px;height:55px;border-radius:50%;animation:fanSpin .7s linear infinite paused}.fanIcon.running{animation-play-state:running}.fanBlade{position:absolute;left:21px;top:2px;width:14px;height:29px;border-radius:50% 50% 45% 45%;background:var(--cyan);transform-origin:7px 25px}.fanBlade:nth-child(2){transform:rotate(90deg)}.fanBlade:nth-child(3){transform:rotate(180deg)}.fanBlade:nth-child(4){transform:rotate(270deg)}.fanHub{position:absolute;left:19px;top:19px;width:18px;height:18px;border-radius:50%;background:var(--hot);border:3px solid #182127}@keyframes fanSpin{to{transform:rotate(360deg)}}.gauge.cold{--gauge-color:var(--cold)}.gauge.good{--gauge-color:var(--good)}.gauge.hot{--gauge-color:var(--danger)}.alert{margin-bottom:14px;padding:13px 15px;border-radius:8px;background:var(--danger);color:#111;display:none}.alert.show{display:flex;justify-content:space-between;align-items:center;gap:12px}.alert button{background:#111;color:var(--text);border-color:#111}.form{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px}.field label{display:block;color:var(--muted);font-size:.82rem;margin-bottom:6px}.field input,.field select{width:100%;padding:10px;background:#10171b;color:var(--text);border:1px solid var(--line);border-radius:6px;font:inherit}.actions{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:17px}.hint{color:var(--muted);font-size:.85rem}h2{font-size:1.05rem;color:var(--cyan);margin:22px 0 11px}.probe{display:flex;flex-direction:column;gap:10px}.probe .value{color:var(--cyan)}
@media(max-width:680px){body{padding:10px}.hero{grid-template-columns:1fr}.hero,.hero .card{min-height:0}.gauge{width:108px;height:108px}header{padding:15px 14px}.brandName{font-size:1rem}.status{font-size:.82rem}nav{overflow-x:auto;scrollbar-width:none}nav::-webkit-scrollbar{display:none}nav button{flex:0 0 auto;min-height:44px}.stateValue{font-size:1.8rem}.card{padding:14px}.field input,.field select,button{min-height:44px}.actions button{flex:1 1 140px}}
@media(max-width:390px){.gaugeRow{gap:8px}.gauge{width:96px;height:96px}.gaugeValue{font-size:1.2rem}.gaugeLabel{font-size:.7rem}.value{font-size:1.35rem}}
</style></head><body><main class="wrap">
<header><div class="brand"><div class="brandName">Boiler Assistant</div><div id="brandMark" hidden></div><div id="brandTemp" hidden></div><div class="envStrip"><div class="headerEnv"><div class="label">Outdoor</div><div class="value" id="mainOutdoor">---</div></div><div class="headerEnv"><div class="label">Humidity</div><div class="value" id="mainHumidity">---</div></div><div class="headerEnv"><div class="label">Pressure</div><div class="value" id="mainPressure">---</div></div></div></div><div style="display:flex;flex-direction:column;align-items:flex-end;gap:5px"><div class="status" id="connection">Connecting...</div></div></header>
<style>@keyframes brandFire{from{transform:rotate(18deg) scale(.9)}to{transform:rotate(32deg) scale(1.1)}}@keyframes brandSmoke{0%{opacity:0;transform:translate(0,5px) scale(.7)}25%{opacity:.8}100%{opacity:0;transform:translate(-8px,-19px) scale(1.3)}}@keyframes brandZzz{0%,100%{transform:translateY(2px)}50%{transform:translateY(-4px)}}</style>
<style>.stateBar{display:flex;justify-content:space-between;align-items:center;padding:11px 15px;margin-bottom:13px;border:1px solid var(--line);border-radius:8px;background:#1b2a30;text-transform:uppercase}.stateBar #burnState{font-size:1.25rem;font-weight:700;color:var(--hot)}.stateHint{font-size:.68rem;color:var(--muted);letter-spacing:.08em}.boilerLayout{display:grid;grid-template-columns:1fr 1.45fr 1fr;gap:13px;align-items:stretch}.sideMetric{text-align:center;display:flex;flex-direction:column;justify-content:center;min-height:245px}.metricNote{margin-top:10px;color:var(--muted);font-size:.8rem}.boilerStage{min-height:245px;display:flex;flex-direction:column;align-items:center;justify-content:center}.boilerArt{position:relative;width:170px;height:190px;margin-top:18px}.boilerRoof{position:absolute;left:8px;top:17px;width:154px;height:35px;border-radius:50%;background:#52636a;border:5px solid #859298;box-shadow:0 5px 0 #26343a}.boilerBody{position:absolute;left:14px;top:34px;width:142px;height:130px;border:5px solid #859298;border-radius:15px 15px 22px 22px;background:linear-gradient(90deg,#34474e,#68797d 45%,#2b3b42);box-shadow:inset 0 -12px 0 #26343a,0 8px 0 #101418}.boilerDoor{position:absolute;left:42px;bottom:12px;width:57px;height:72px;border:5px solid #1b272c;border-radius:27px 27px 8px 8px;background:#101418;box-shadow:inset 0 0 0 3px #69787c}.boilerFire{position:absolute;left:20px;bottom:13px;width:18px;height:30px;border-radius:70% 35% 65% 35%;background:var(--hot);opacity:0;transform:rotate(24deg);box-shadow:0 0 16px var(--hot)}.boilerArt.burning .boilerFire{opacity:1;animation:brandFire .7s ease-in-out infinite alternate}.ember{position:absolute;bottom:8px;width:6px;height:4px;border-radius:50%;background:var(--hot);opacity:0}.emberOne{left:17px}.emberTwo{right:15px}.boilerArt.idle .ember{opacity:.75;box-shadow:0 0 7px var(--hot);animation:emberPulse 1.3s ease-in-out infinite alternate}.chimney{position:absolute;left:66px;top:-2px;width:38px;height:45px;background:#4b5b60;border:4px solid #859298;border-bottom:0;border-radius:6px 6px 0 0}.chimney i{position:absolute;width:9px;height:9px;border:2px solid var(--muted);border-radius:50%;opacity:0}.boilerArt.burning .chimney i{animation:brandSmoke 2.2s ease-out infinite}.chimney i:nth-child(1){left:12px;top:-12px}.chimney i:nth-child(2){left:2px;top:-22px;animation-delay:.7s}.chimney i:nth-child(3){left:17px;top:-31px;animation-delay:1.4s}.boilerCaption{color:var(--hot);font-weight:700;font-size:.85rem;margin-top:5px}.miniGauge{width:92px;height:92px;margin:16px auto 0}.miniGauge .gaugeValue{font-size:.9rem}.fanCard{display:flex;justify-content:center;align-items:center;gap:30px;margin-top:13px;min-height:125px}.fanCard>.label{margin:0}.fanCard .value{font-size:1.1rem;color:var(--muted)}@keyframes emberPulse{from{opacity:.35}to{opacity:1}}@media(max-width:680px){.boilerLayout{grid-template-columns:1fr 1.3fr 1fr;gap:7px}.sideMetric{min-height:205px;padding:10px}.boilerStage{min-height:205px;padding:5px}.boilerArt{transform:scale(.8);margin:-8px 0 -12px}.stateBar #burnState{font-size:1.05rem}.fanCard{min-height:105px;gap:15px}}
</style>
<style>.stateBar{justify-content:center}.boilerLayout{grid-template-columns:130px minmax(170px,1fr) 130px}.boilerArt{transform:scale(.72);margin:-18px 0 -30px}.boilerStage{min-height:205px}.fanSide{align-items:center}.fanSide .fanGauge{width:104px;height:104px}.waterUnderBoiler{text-align:center;margin-top:-4px}.waterUnderBoiler .label{margin-bottom:3px}.waterUnderBoiler .value{font-size:1.25rem;color:var(--cyan)}.exhaustSide{align-items:center}.exhaustReadout{text-align:center;margin-top:10px}.exhaustReadout .value{color:var(--cyan);font-size:1.25rem}.boilerArt.burning .chimney i:nth-child(4),.boilerArt.burning .chimney i:nth-child(5){animation:brandSmoke 2.2s ease-out infinite}.boilerArt.burning .chimney i:nth-child(4){left:20px;top:-16px;animation-delay:.35s}.boilerArt.burning .chimney i:nth-child(5){left:-5px;top:-28px;animation-delay:1.05s}.boilerZzz{position:absolute;left:42px;top:-30px;width:50px;color:var(--cyan);font-size:1rem;font-weight:700;opacity:0}.boilerArt.idle .boilerZzz{opacity:1}.boilerZzz span{position:absolute;opacity:0;animation:boilerZzz 2.8s ease-out infinite}.boilerZzz span:nth-child(1){left:24px;top:18px;animation-delay:0s}.boilerZzz span:nth-child(2){left:10px;top:8px;animation-delay:.9s}.boilerZzz span:nth-child(3){left:31px;top:0;animation-delay:1.8s}@keyframes boilerZzz{0%{opacity:0;transform:translate(0,8px) scale(.7)}20%{opacity:1}100%{opacity:0;transform:translate(-10px,-24px) scale(1.2)}}@media(max-width:680px){.boilerLayout{grid-template-columns:100px minmax(140px,1fr) 100px;gap:7px}.boilerArt{transform:scale(.58);margin:-30px 0 -42px}.boilerStage{min-height:175px}.fanSide .fanGauge{width:84px;height:84px}.waterUnderBoiler .value,.exhaustReadout .value{font-size:1.05rem}}</style>
<style>.boilerArt.burning .chimney i{width:13px;height:13px;border-width:3px}.boilerArt.burning .chimney i:nth-child(6){left:8px;top:-39px;animation:brandSmoke 2.2s ease-out infinite;animation-delay:.7s}.boilerArt.burning .chimney i:nth-child(7){left:26px;top:-48px;animation:brandSmoke 2.2s ease-out infinite;animation-delay:1.4s}.boilerZzz{left:-2px;top:-22px;width:62px;font-size:1.65rem}.boilerZzz span:nth-child(1){left:12px;top:10px}.boilerZzz span:nth-child(2){left:0;top:-3px}.boilerZzz span:nth-child(3){left:20px;top:-16px}.boilerZzz span:nth-child(4){left:39px;top:-26px;animation-delay:2.2s}.boilerZzz span:nth-child(5){left:25px;top:-38px;animation-delay:2.7s}</style>
<style>.brandName{font-size:1.38rem}.stateBar #burnState{font-size:1.48rem}@media(max-width:680px){.brandName{font-size:1.18rem}.stateBar #burnState{font-size:1.22rem}}</style>
<style>.brandName{font-size:1.55rem}.exhaustSide{overflow:hidden;padding-left:8px;padding-right:8px}.exhaustSide .miniGauge{width:74px;height:74px;margin-top:4px}.waterUnderBoiler,.exhaustReadout{text-align:center;margin-top:6px}.waterUnderBoiler .fanPercent,.exhaustReadout .fanPercent{margin-top:0}.waterUnderBoiler .label,.exhaustReadout .label{margin-top:3px}.waterUnderBoiler .value,.exhaustReadout .value{font-size:1.25rem;font-weight:700;color:var(--cyan)}@media(max-width:680px){.brandName{font-size:1.28rem}.exhaustSide .miniGauge{width:66px;height:66px}.waterUnderBoiler .value,.exhaustReadout .value{font-size:1.05rem}}</style>
<style>.boilerDoor .boilerFire{width:23px;height:38px;left:16px;bottom:10px}.boilerDoor .flameTwo{width:15px;height:27px;left:31px;bottom:9px;animation-delay:.18s!important;transform:rotate(-14deg)!important}.boilerDoor .flameThree{width:12px;height:22px;left:8px;bottom:8px;animation-delay:.34s!important;transform:rotate(42deg)!important}.boilerArt.burning .boilerFire{opacity:1;animation:boilerFlame .65s ease-in-out infinite alternate}.boilerArt.idle .boilerFire{opacity:.18}.boilerArt.idle .flameTwo,.boilerArt.idle .flameThree{opacity:.08}.boilerArt.idle .ember{width:8px;height:5px;opacity:1;box-shadow:0 0 10px var(--hot)}@keyframes boilerFlame{from{filter:brightness(.9);transform:scale(.9) rotate(20deg)}to{filter:brightness(1.3);transform:scale(1.12) rotate(30deg)}}</style>
<style>.brandName{font-size:1.82rem}@media(max-width:680px){.brandName{font-size:1.48rem}}</style>
<style>.boilerShield{display:none;position:absolute;z-index:4;left:25px;top:8px;width:92px;height:112px;border:4px solid #55c4c7;border-radius:48% 48% 52% 52%;background:#14383dcc;box-shadow:0 0 16px #55c4c799,inset 0 0 12px #55c4c766;align-items:center;justify-content:center;color:#55c4c7;font-size:2.2rem;font-weight:700}.boilerArt.guardian .boilerShield{display:flex}.boilerArt.guardian .boilerFire,.boilerArt.guardian .chimney i,.boilerArt.guardian .boilerZzz{display:none}.boilerArt.guardian .ember{opacity:1;width:8px;height:5px;box-shadow:0 0 12px var(--hot);animation:emberPulse 1.3s ease-in-out infinite alternate}.boilerArt.guardian .boilerDoor{border-color:#55c4c7}</style>
<style>.metricReadout{text-align:center;margin-top:6px}.metricReadout .label{margin-top:3px}.fanSide,.exhaustSide{justify-content:center}</style>
<style>.historyTitle{font-size:1.25rem;font-weight:700;color:var(--hot)}.historyHint{margin:4px 0 14px;color:var(--muted);font-size:.82rem}.historyList{display:grid;gap:8px}.historyRow{display:grid;grid-template-columns:38px 1fr 1fr;gap:8px;align-items:center;padding:10px;border:1px solid var(--line);border-radius:7px;background:#172228}.historyRow .index{color:var(--muted);font-weight:700}.historyRow .historyValue{font-weight:700;color:var(--cyan)}.historyLabel{display:block;color:var(--muted);font-size:.65rem;text-transform:uppercase;letter-spacing:.06em}.historyEmpty{color:var(--muted);padding:16px 0}@media(max-width:390px){.historyRow{grid-template-columns:28px 1fr 1fr;gap:5px;font-size:.9rem}}</style>
<style>.historyViewport{width:100%;height:230px;margin:8px 0 20px;overflow-x:auto;overflow-y:hidden;border:1px solid var(--line);border-radius:6px;background:#0b1115;touch-action:pan-x}.historyGraph{display:block;height:228px;min-width:100%;background:#0b1115}.historyGraphEmpty{color:var(--muted);padding:80px 0;text-align:center}@media(max-width:390px){.historyViewport{height:210px}.historyGraph{height:208px}}</style>
<style>.mainWaterTitle{margin-top:13px}.mainWaterHint{margin-bottom:7px}.timeScale{display:flex;justify-content:space-between;margin:-14px 4px 13px;color:var(--muted);font-size:.62rem}.timeScale span{white-space:nowrap}</style>
<style>#alert{display:none}.boilerArt .chimney i{border-color:var(--smoke-color,#68767a)}</style>
<style>header .envStrip{display:flex;flex:1 1 auto;justify-content:flex-end;gap:12px;margin:0;padding:0;background:transparent}.headerEnv{display:flex;flex-direction:column;align-items:flex-end;min-width:48px}.headerEnv .label{font-size:.55rem;margin:0;letter-spacing:.05em}.headerEnv .value{font-size:.82rem;line-height:1.1;color:var(--cyan);white-space:nowrap}@media(max-width:680px){header{gap:8px;padding:12px}.headerEnv{min-width:40px}.headerEnv .value{font-size:.7rem}.headerEnv .label{font-size:.48rem}.status{font-size:.72rem}}</style>
<style>.brand .envStrip{justify-content:center;gap:9px;margin-top:3px}.brand .headerEnv{align-items:center;min-width:42px}.brand .headerEnv .value{font-size:.72rem}.brand .headerEnv .label{font-size:.48rem}</style>
<style>.mainChart{margin:13px 0 0;padding:10px 12px;background:#182127;border:1px solid var(--line);border-radius:8px}.mainChartTitle{font-size:.68rem;color:var(--muted);text-transform:uppercase;letter-spacing:.08em;margin-bottom:5px}.mainChartCanvas{display:block;width:100%;height:74px;background:#0b1115;border-radius:5px}</style>
<style>.exhaustDial{flex:0 0 104px;width:104px;height:104px;border-radius:50%;background:#182127;--dial-color:#68767a}.exhaustDial svg{display:block;width:100%;height:100%}.exhaustTrack,.exhaustZone{fill:none;stroke-width:7;transform:rotate(135deg);transform-origin:50px 50px}.exhaustTrack{stroke:#52636a}#exhaustBand{stroke-width:16}.exhaustTicks{fill:none;stroke:var(--text);stroke-width:1}.exhaustScale{fill:var(--muted);font:7px sans-serif;text-anchor:middle}.exhaustNeedle{stroke:var(--text);stroke-width:2;stroke-linecap:round;transform:rotate(var(--needle,-135deg));transform-origin:50px 50px}.exhaustHub{fill:var(--dial-color)}.exhaustDial.invalid .exhaustNeedle{display:none}</style>
<style>main>nav>.tab.active{font-weight:inherit}@media(max-width:680px){main>nav>.tab{flex:1 1 auto}}nav{margin-left:-10px;margin-right:-10px;padding:12px 10px calc(12px + env(safe-area-inset-bottom));background:#182127;border-top:1px solid var(--line)}@media(min-width:681px){nav{margin-left:-22px;margin-right:-22px;padding-left:22px;padding-right:22px}}</style>
<style>.fanSide .fanGauge{box-sizing:border-box;flex:0 0 104px;width:104px;height:104px}@media(max-width:680px){.fanSide .fanGauge{flex-basis:84px;width:84px;height:84px}}</style>
<style>.stateBar.alarm{background:var(--danger);border-color:var(--danger);color:#111;min-height:70px;gap:12px;flex-wrap:wrap;width:100%}.stateBar.alarm #burnState{flex:1 1 100%;font-size:.95rem;color:#111;line-height:1.2;max-width:100%;white-space:normal;overflow-wrap:anywhere;text-align:center}.stateReset{display:none;border:2px solid #111;border-radius:6px;padding:7px 12px;background:#111;color:#fff;font:inherit;font-weight:700;cursor:pointer}.stateBar.alarm .stateReset{display:block;margin:0 auto}</style>
<style>.boilerLayout{grid-template-columns:minmax(0,130px) minmax(0,1fr) minmax(0,130px)}.boilerLayout>.card{min-width:0}@media(max-width:680px){.boilerLayout{grid-template-columns:minmax(0,100px) minmax(0,1fr) minmax(0,100px)}}</style>
<style>.boilerLayout{display:grid!important;grid-template-columns:minmax(0,1fr) minmax(170px,1.35fr) minmax(0,1fr)!important;grid-template-rows:auto auto;align-items:stretch;gap:13px;width:100%;overflow:visible}.boilerLayout>.card{min-width:0;width:auto}.leftStack{display:grid;grid-template-rows:1fr 1fr;gap:13px;min-width:0}.leftStack>.card{min-width:0}.boilerStage,.exhaustSide{grid-row:span 2}.waterSide,.fanSide{min-height:0}.stateBar.alarm{position:relative;z-index:2}@media(max-width:680px){.boilerLayout{grid-template-columns:minmax(0,1fr) minmax(140px,1.35fr) minmax(0,1fr)!important;gap:7px}.leftStack{gap:7px}.boilerArt{transform:scale(.48);margin:-38px 0 -50px}}</style>
<style>.leftStack{grid-row:span 2;grid-template-rows:1fr}.leftStack>.fanSide{height:100%}@media(max-width:390px){.boilerLayout{grid-template-columns:minmax(0,1fr) minmax(90px,1.15fr) minmax(80px,1fr)!important}.exhaustSide{padding:7px 3px}}</style>
<style>.maintenancePanel{display:grid;gap:13px;margin-top:13px}.selfCleanPanel{display:flex;align-items:center;justify-content:space-between;gap:14px;flex-wrap:wrap;padding:14px 17px;background:linear-gradient(145deg,#213039,#182228);border:1px solid var(--line);border-radius:10px;box-shadow:0 8px 18px #080b0d45;color:var(--text)}.selfCleanPanel.due{border-color:var(--hot)}.selfCleanPanel.active{border-color:var(--cyan)}.selfCleanInfo{min-width:0}.selfCleanTitle{font-size:.78rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin-bottom:4px}.selfCleanValue{font-size:1.1rem;font-weight:700;color:var(--cyan)}.selfCleanPanel.due .selfCleanValue{color:var(--hot)}.selfCleanPanel.active .selfCleanValue{color:var(--cyan)}.selfCleanCount{margin-top:2px;color:var(--muted);font-size:.8rem}.selfCleanPanel button{margin:0;flex:0 0 auto}</style>
<section id="state" class="active"><div class="stateBar"><span id="burnState">---</span><button class="stateReset" id="stateReset">Reset alarm</button></div><div class="boilerLayout">
<style>.boilerLayout{grid-template-columns:repeat(3,minmax(0,1fr))!important;grid-template-rows:auto}.leftStack{display:contents}.boilerLayout .card{display:grid;grid-template-rows:170px auto minmax(48px,auto);grid-row:auto;align-items:start;justify-content:stretch;min-height:0;padding:12px 8px;text-align:center}.mainMetricGraphic{height:170px;display:flex;align-items:center;justify-content:center;min-width:0;overflow:hidden}.mainMetricGraphic .boilerArt{flex:0 0 170px;transform:scale(.65);margin:0}.mainMetricGraphic .thermometer{height:120px;margin:0}.boilerLayout .metricReadout{margin:0}.boilerLayout .fanPercent{font-size:1.35rem;line-height:1.3;font-weight:700;color:var(--cyan);margin:0;white-space:nowrap;font-variant-numeric:tabular-nums}.boilerLayout .metricReadout .label{font-size:.68rem;line-height:1.4;margin:4px 0 0}.metricDetails{padding-top:8px;min-width:0}@media(max-width:680px){.boilerLayout .card{grid-template-rows:145px auto minmax(48px,auto);padding:8px 3px}.mainMetricGraphic{height:145px}.mainMetricGraphic .boilerArt{transform:scale(.5)}.mainMetricGraphic .thermometer{height:100px}.boilerLayout .fanPercent{font-size:1.05rem}.boilerLayout .metricReadout .label{font-size:.6rem}.mainMetricGraphic .fanGauge{flex:0 0 66px;width:66px;height:66px}}</style>
<style>.mainMetricGraphic .fanGauge{flex:0 0 120px;width:120px;height:120px;box-shadow:none}.mainMetricGraphic .exhaustDial{flex:0 1 160px;width:160px;height:auto;max-width:100%;aspect-ratio:1}@media(max-width:680px){.mainMetricGraphic .fanGauge{flex-basis:80px;width:80px;height:80px}.mainMetricGraphic .exhaustDial{flex-basis:120px;width:120px}}</style>
<div class="leftStack"><div class="card sideMetric fanSide"><div class="mainMetricGraphic"><div class="gauge fanGauge" id="fanGauge"><div class="fanIcon" id="fanIcon"><span class="fanBlade"></span><span class="fanBlade"></span><span class="fanBlade"></span><span class="fanBlade"></span><span class="fanHub"></span></div></div></div><div class="metricReadout"><div class="fanPercent" id="fan">---</div><div class="label">Fan</div></div><div class="metricDetails" aria-hidden="true"></div></div></div>
<div class="card boilerStage"><div class="mainMetricGraphic"><div class="boilerArt" id="boilerArt"><div class="chimney"><i></i><i></i><i></i><i></i><i></i><i></i><i></i><div class="boilerZzz"><span>Z</span><span>z</span><span>z</span><span>Z</span><span>z</span></div></div><div class="boilerRoof"></div><div class="boilerBody"><div class="boilerDoor"><div class="boilerFire"></div><div class="boilerFire flameTwo"></div><div class="boilerFire flameThree"></div><div class="ember emberOne"></div><div class="ember emberTwo"></div></div><div class="boilerShield">!</div></div></div></div><div class="metricReadout waterUnderBoiler"><div class="fanPercent" id="tank">---</div><div class="label">Water temp</div></div><div class="metricDetails" aria-hidden="true"></div></div>
<div class="card sideMetric exhaustSide"><div class="mainMetricGraphic"><div class="exhaustDial invalid" id="exhaustGauge" role="img" aria-label="Exhaust temperature unavailable"><svg viewBox="0 0 100 100" aria-hidden="true"><circle class="exhaustTrack" cx="50" cy="50" r="38" pathLength="100" stroke-dasharray="75 25"/><circle class="exhaustZone" id="exhaustLow" cx="50" cy="50" r="38" pathLength="100" stroke="#f6c65b" visibility="hidden"/><circle class="exhaustZone" id="exhaustBand" cx="50" cy="50" r="38" pathLength="100" stroke="var(--good)" visibility="hidden"/><circle class="exhaustZone" id="exhaustHigh" cx="50" cy="50" r="38" pathLength="100" stroke="var(--danger)" visibility="hidden"/><path class="exhaustTicks" d="M22 78l4-4M13 35l6 2M50 10v6M87 35l-6 2M78 78l-4-4"/><g class="exhaustScale"><text x="18" y="90">100</text><text x="50" y="7">500</text><text x="81" y="90">900</text></g><line class="exhaustNeedle" x1="50" y1="55" x2="50" y2="20"/><circle class="exhaustHub" cx="50" cy="50" r="3"/></svg></div></div><div class="metricReadout"><div class="fanPercent" id="exhaust">---</div><div class="label">Exhaust</div></div></div>
</div><div class="historyTitle mainWaterTitle">Boiler trends</div><div class="historyHint mainWaterHint">Live readings and SD history.</div><div class="mainChartLegend"><span style="border-color:#ff6b68">Exhaust (F)</span><span style="border-color:#4ba3ff">Water (F)</span><span style="border-color:#f6c65b">Fan (%)</span></div><div class="historyViewport" id="historyViewport"><canvas class="historyGraph" id="burnGraph" role="img" aria-label="Water, exhaust and fan history"></canvas></div><div class="timeScale"><span id="timeScale0">Now</span><span id="timeScale1"></span><span id="timeScale2"></span><span id="timeScale3"></span><span id="timeScale4"></span></div><div class="maintenancePanel"><div id="alert" class="alert"><span id="alertText"></span><button id="alertReset">Reset alarm</button></div><div id="selfCleanPanel" class="selfCleanPanel"><div class="selfCleanInfo"><div class="selfCleanTitle">Self-clean</div><div class="selfCleanValue" id="selfCleanStatus">Disabled</div><div class="selfCleanCount" id="selfCleanCount"></div></div><button id="selfCleanNow" onclick="selfCleanNow()">Run on next burn</button></div></div></section>
<section id="settings"><div class="card"><div class="form">
<div class="field"><label>Exhaust setpoint (F)</label><input id="exhaust_setpoint" type="number" min="200" max="900"></div>
<div class="field"><label>Deadband (F)</label><input id="deadband" type="number" min="1" max="100"></div>
<div class="field"><label>Damper delay (s, 0=skip)</label><input id="damper_delay" type="number" min="0" max="300" step="1"></div>
<div class="field"><label>Boost time (s, 0=skip)</label><input id="boost_time" type="number" min="0" max="600"></div>
<div class="field"><label>Boost fan (%, within fan limits; next burn)</label><input id="boost_fan_pct" type="number" min="0" max="100" step="1"></div>
<div class="field"><label>Fan minimum (%)</label><input id="clamp_min" type="number" min="0" max="100"></div>
<div class="field"><label>Fan maximum (%)</label><input id="clamp_max" type="number" min="0" max="100"></div>
<div class="field"><label>Fan mode</label><select id="deadzone_fan"><option value="0">Fan allowed off</option><option value="1">Fan always on</option></select></div>
<div class="field"><label>Burn curve learning</label><select id="adaptive_learning"><option value="0">Off - safe fixed gain</option><option value="1">On - qualified burns only</option></select></div>
<div class="field"><label>Tank low (F)</label><input id="tank_low" type="number" min="40" max="189"></div>
<div class="field"><label>Tank high (F)</label><input id="tank_high" type="number" min="40" max="189"></div>
<div class="field"><label>Guardian minutes</label><input id="ember_minutes" type="number" min="1" max="120"></div>
<div class="field"><label>Flue low (F)</label><input id="flue_low" type="number" min="50" max="500"></div>
<div class="field"><label>Flue recovery (F)</label><input id="flue_recovery" type="number" min="50" max="500"></div>
<div class="field"><label>Outdoor units</label><select id="env_units"><option value="0">Imperial (F, inHg)</option><option value="1">Metric (C, kPa)</option></select></div>
<div class="field"><label>Mode</label><select id="self_clean_enabled"><option value="0">Off</option><option value="1">Automatic</option></select></div>
<div class="field"><label>Every (burns)</label><input id="self_clean_interval" type="number" min="1" max="255"></div>
<div class="field"><label>Start time</label><select id="self_clean_start_hour"></select></div>
<div class="field"><label>End time</label><select id="self_clean_end_hour"></select></div>
<div class="field"><label>Timezone</label><select id="self_clean_timezone"><option value="-600|0">Hawaii (UTC-10)</option><option value="-540|1">Alaska (UTC-9, DST)</option><option value="-480|1">Pacific (UTC-8, DST)</option><option value="-420|1">Mountain (UTC-7, DST)</option><option value="-360|1">Central (UTC-6, DST)</option><option value="-300|1">Eastern (UTC-5, DST)</option><option value="-240|1">Atlantic (UTC-4, DST)</option><option value="-210|1">Newfoundland and Labrador (UTC-3:30, DST)</option><option value="-420|0">Arizona (UTC-7, no DST)</option><option value="-360|0">Saskatchewan (UTC-6, no DST)</option><option value="-420|0">Yukon (UTC-7, no DST)</option></select></div>
<div class="field"><label>Boiler name (start-up screen)</label><input id="display_name" type="text" maxlength="20" placeholder="e.g. SMITH FARM"></div>
<div id="notificationSettings" class="card" hidden role="tabpanel"><h2>Notifications</h2><div class="form">
<div class="field"><label>Push notifications (ntfy)</label><select id="push_enabled"><option value="0">Off</option><option value="1">On</option></select></div>
<div class="field"><label>ntfy topic (letters, numbers, - _)</label><input id="push_topic" type="text" maxlength="32" placeholder="e.g. my-boiler-7xk2"></div>
</div><p class="hint">Install ntfy (iOS/Android). Subscribe to your topic, turn On, Save settings, then test. Alerts: high temp, probe faults, Ember Guardian.</p><button id="pushTest">Send test notification</button></div>
<div class="field"><label>Boiler make</label><input id="boiler_make" type="text" maxlength="31" placeholder="e.g. Central Boiler"></div>
<div class="field"><label>Boiler model</label><input id="boiler_model" type="text" maxlength="31" placeholder="e.g. Classic Edge 750"></div>
<div class="field"><label>Share burn data (needs SD card)</label><select id="share_data"><option value="0">No</option><option value="1">Yes, I agree</option></select><span class="hint" id="share_status"></span></div>
 </div><div class="actions"><button class="primary" id="save">Save settings</button><button id="resetAlarm">Reset active alarm</button><button id="resetAdaptiveLearning">Reset learned fan tuning</button><button id="token">Set control password</button><span class="hint" id="message"></span></div></div></section>
<section id="sensors"><div class="grid"><div class="card"><div class="label">Outdoor temperature</div><div class="value" id="outdoor">---</div></div><div class="card"><div class="label">Humidity</div><div class="value" id="humidity">---</div></div><div class="card"><div class="label">Pressure</div><div class="value" id="pressure">---</div></div><div class="card"><div class="label">Exhaust sensor</div><div class="value" id="exhaustOk">---</div></div><div class="card"><div class="label">Tank sensor</div><div class="value" id="tankOk">---</div></div><div class="card"><div class="label">WiFi</div><div class="value" id="wifi">---</div></div></div><h2>Water probes</h2><div class="grid" id="waterProbes"></div></section>
<section id="history"><div class="card"><div class="historyTitle">Burn times</div><div class="historyHint">Completed burns from the last 24 hours, read from SD. Burn time is start to end. Between burns is from the previous recorded end to this start, including any time powered off. A burn appears here after it ends.</div><div id="burnHistoryStatus" class="historyHint" role="status">Loading SD history...</div><div class="historyList" id="burnHistory"></div></div></section>
<nav><button class="tab active" data-target="state">Main</button><button class="tab" data-target="history">Burn Times</button><button class="tab" data-target="settings">Settings</button></nav>
</main><script>
const ids=['exhaust_setpoint','deadband','damper_delay','boost_time','boost_fan_pct','clamp_min','clamp_max','deadzone_fan','adaptive_learning','tank_low','tank_high','ember_minutes','flue_low','flue_recovery','env_units','self_clean_enabled','self_clean_interval','self_clean_start_hour','self_clean_end_hour','self_clean_timezone','push_enabled','push_topic','display_name','boiler_make','boiler_model','share_data'];
const tokenKey='boiler-control-token';
let lastProbeSignature='';
const LIVE_REFRESH_MS=2000;
const SLOW_REFRESH_MS=60000;
const REQUEST_TIMEOUT_MS=15000;
let requestQueue=Promise.resolve();
const pendingGets=new Map();
let lastState=null;
let lastStateMs=0;
let lastSettingsMs=0;
let lastHistoryMs=0;
let historySnapshot=null;
let pollTimer=null;
let pollRunning=false;
let lastPollFinishMs=0;
let requestDelay=5000;
let requestRetryAt=0;
function updateConnectionStatus(){const fresh=!!lastStateMs&&Date.now()-lastStateMs<=10000;text('connection',fresh?'Online':'Offline - readings stale');return fresh}
function requestFailed(error,url='/api/state'){if(error.status&&error.status<500)return;if(url!=='/api/state'){updateConnectionStatus();return}lastStateMs=0;requestDelay=Math.min(30000,requestDelay*2);requestRetryAt=Date.now()+requestDelay;updateConnectionStatus()}
// Hold the queue until the response body is consumed, not just until headers arrive.
function requestJson(url,options={}){ const shared=Object.keys(options).length===0;if((options.method||'GET')!=='GET')pendingGets.clear();if(shared&&pendingGets.has(url))return pendingGets.get(url);const run=async()=>{const wait=requestRetryAt-Date.now();if(wait>0)await new Promise(resolve=>setTimeout(resolve,wait));const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),REQUEST_TIMEOUT_MS);try{const response=await fetch(url,{cache:'no-store',...options,signal:controller.signal});const data=await response.json();if(!response.ok)throw Object.assign(new Error(data.error||'HTTP '+response.status),{status:response.status});if(url==='/api/state'){requestDelay=5000;requestRetryAt=0}return data}catch(error){requestFailed(error,url);throw error}finally{clearTimeout(timer)}}; const result=requestQueue.then(run); requestQueue=result.catch(error=>{console.error('Dashboard request failed:',url,error)});if(shared){pendingGets.set(url,result);const clear=()=>{if(pendingGets.get(url)===result)pendingGets.delete(url)};result.then(clear,clear)}return result;}
function reportRequestError(error){if(error.status===401){localStorage.removeItem(tokenKey);showAlert('Wrong or missing password');return}if(!error.status||error.status>=500){showAlert(updateConnectionStatus()?error.message:'Readings stale - retrying');return}showAlert(error.message)}
function runAction(action){return ()=>action().catch(reportRequestError)}
function activeTab(id){return id==='sensors'?activeTab('settings')&&!document.getElementById(id).hidden:document.getElementById(id).classList.contains('active')}
async function pollDashboard(){if(pollRunning||document.hidden)return;pollRunning=true;const liveRefreshMs=activeTab('state')?1000:LIVE_REFRESH_MS;try{if(Date.now()<requestRetryAt)return;if(!lastState||Date.now()-lastStateMs>=liveRefreshMs)await state();for(const refresh of [refreshSdLog,refreshSeasonal,refreshWaterCalibration]){if(document.hidden||requestRetryAt)return;await refresh()}if(document.hidden||requestRetryAt)return;if(activeTab('settings')&&(!lastSettingsMs||Date.now()-lastSettingsMs>=SLOW_REFRESH_MS))await settings();if(document.hidden||requestRetryAt)return;await refreshCharts()}catch(error){reportRequestError(error)}finally{pollRunning=false;lastPollFinishMs=Date.now();if(!document.hidden)pollTimer=setTimeout(pollDashboard,Math.max(0,liveRefreshMs-(lastStateMs?Date.now()-lastStateMs:0),requestRetryAt-Date.now()))}}
function pollNow(){clearTimeout(pollTimer);if(!pollRunning&&!document.hidden)pollDashboard()}
// Self-healing: if the page is visible but the poll loop has gone silent (for
// example after a mobile browser reported the tab hidden during load), restart
// it without needing a tab switch. Does not run during a real network backoff.
function pollWatchdog(){if(!document.hidden&&!pollRunning&&Date.now()>=requestRetryAt&&Date.now()-lastPollFinishMs>5000)pollNow()}
document.addEventListener('visibilitychange',()=>{clearTimeout(pollTimer);if(!document.hidden)pollNow()});
function token(){return localStorage.getItem(tokenKey)||''}
function ensureToken(){if(token())return true;const v=prompt('Enter the Control/API Password saved during WiFi setup to save settings');if(v===null||v==='')return false;localStorage.setItem(tokenKey,v);return true}
function set(id,v){const e=document.getElementById(id);if(!e||v===undefined||v===null)return;if(e===document.activeElement)return;if(e.tagName==='SELECT'){const s=String(typeof v==='boolean'?Number(v):v);for(let i=0;i<e.options.length;i++){if(e.options[i].value===s){e.value=s;if(id==='env_units')syncSeasonUnits();return}}return}e.value=v}
function text(id,v){const e=document.getElementById(id);if(e)e.textContent=v}
function renderProbes(probes){const root=document.getElementById('waterProbes');if(!root||!activeTab('sensors'))return;probes=probes||[];const sig=JSON.stringify(probes.map(p=>[p.id,p.name,p.controls_tank]));if(sig!==lastProbeSignature&&!root.contains(document.activeElement)){lastProbeSignature=sig;root.innerHTML='';const names=['MAIN TANK','OTHER TANK','TANK BOTTOM','TANK TOP','Loop 1 Supply','Loop 1 Return','Loop 2 Supply','Loop 2 Return','Loop 3 Supply','Loop 3 Return','Loop 4 Supply','Loop 4 Return','L1 SUPPLY','L1 RETURN','L2 SUPPLY','L2 RETURN','EXTRA'];probes.forEach(p=>{const card=document.createElement('div');card.className='card probe';card.innerHTML='<div class="label"></div><div class="value"></div><select></select><button>Save name</button>'+(p.controls_tank?'':'<button>Use for tank control</button>');const[label,,select,save,tank]=card.children;label.textContent=p.name+(p.controls_tank?' (controls tank)':' (monitor only)')+' - ID '+(p.id||'?');(names.includes(p.name)?names:[p.name,...names]).forEach(n=>select.add(new Option(n,n,false,n===p.name)));save.onclick=()=>saveProbe(p.index,{name:select.value});if(tank)tank.onclick=()=>confirm('Control the burn from '+p.name+' (ID '+(p.id||'?')+')?')&&saveProbe(p.index,{tank:true});root.append(card)})}probes.forEach((p,i)=>{const c=root.children[i];if(c)c.children[1].textContent=p.sensor_ok===false||p.temp_f===null?'NO SIGNAL':Math.round(p.temp_f)+' F'})}
async function saveProbe(index,changes){if(!ensureToken())return;try{const j=await requestJson('/api/probe',{method:'POST',headers:{'Content-Type':'application/json','X-Boiler-Token':token()},body:JSON.stringify({index,...changes})});if(!j.ok)throw new Error(j.error||'Not saved');lastProbeSignature='';await state()}catch(error){reportRequestError(error)}}
async function resetAlarm(){const j=await requestJson('/api/reset',{method:'POST',headers:{'X-Boiler-Token':token()}});showAlert(j.ok?'':(j.error||'Alarm reset rejected'));await state()}
function formatDuration(seconds){if(!seconds)return '--';const minutes=Math.floor(seconds/60);const hours=Math.floor(minutes/60);if(hours)return hours+'h '+(minutes%60)+'m';return minutes+'m '+(seconds%60)+'s'}
function showAlert(s){const e=document.getElementById('alert');const t=document.getElementById('alertText');if(t)t.textContent=s||'';if(e)e.classList.toggle('show',!!s);syncStateAlarm()}
function syncBoilerArt(){const art=document.getElementById('boilerArt');const state=document.getElementById('burnState')?.textContent||'IDLE';if(!art)return;const guardian=state==='EMBER_GUARD'||state.indexOf('EMBER GUARDIAN')>=0;const idle=state==='IDLE'&&!guardian;art.classList.toggle('guardian',guardian);art.classList.toggle('idle',idle);art.classList.toggle('burning',!idle&&!guardian)}
function syncStateAlarm(){const bar=document.querySelector('.stateBar');const state=document.getElementById('burnState');const message=document.getElementById('alertText')?.textContent||'';if(!bar||!state)return;if(message){if(!state.dataset.normal)state.dataset.normal=state.textContent;state.textContent=message;bar.classList.add('alarm')}else{if(state.dataset.normal)state.textContent=state.dataset.normal;delete state.dataset.normal;bar.classList.remove('alarm')}}
function renderFanDiagnostics(s){
 const f=s?.fan_diag;
 const available=Number.isInteger(f)&&f>=0&&f<=255;
 text('fanZc',!available?'Unavailable':f&16?'Electrical noise detected - retrying':f&8?'Stable 60 Hz timing':f&4?'Valid 60 Hz timing - stabilizing':f&2?'Pulses detected - timing not valid':'No recent zero-cross signal');
 text('fanDriver',!available?'Unavailable':!(f&1)?(f&128?'Output inhibited':'PWM fallback available')+' - phase timer or ZC interrupt unavailable':f&128&&!(f&8)?'Output inhibited - waiting for stable ZC':!(f&64)?'Off - no fan output requested':f&32&&f&4?'Phase control - synchronized to ZC':'PWM fallback - not synchronized to ZC');
 text('diagnosticCodes',diagnosticCodes(s).join('\n'));
}
function diagnosticCodes(s){
 if(!s)return ['DATA01 [UNKNOWN] Live controller data unavailable'];
 const rows=[],f=s.fan_diag;
 if(Number.isInteger(f)&&f>=0&&f<=255){
  if(!(f&1))rows.push('FAN01 [ACTIVE] Phase timer or ZC IRQ unavailable');
  if(f&16)rows.push('ZC02 [ACTIVE] Noise suppression; retrying');
  else if(!(f&2))rows.push('ZC01 [INFO] No ZC signal. If AC is expected, check the module/signal connection.');
  else if(!(f&4))rows.push('ZC02 [ACTIVE] Invalid pulse timing');
  else if(!(f&8))rows.push('ZC03 [INFO] Valid pulses; stabilizing');
  else rows.push('ZC00 [OK] Stable zero-cross signal');
  if(!(f&128)&&(f&64)&&(!(f&32)||!(f&4)))rows.push('FAN02 [INFO] PWM fallback selected, not a measured fan speed.');
 }else rows.push('ZC04 [UNKNOWN] Driver diagnostics unavailable');
 if(s.safety_state===1)rows.push('SAFE01 [LATCHED] High-temperature lockout');
 if(s.safety_state===2)rows.push('SAFE02 [LATCHED] Sensor lockout');
 if(s.alert?.includes('EMBER GUARDIAN'))rows.push('SAFE03 [LATCHED] Ember Guardian');
 if(s.exhaust_sensor_ok===false||s.exhaust_fallback)rows.push('TEMP01 [ACTIVE] Exhaust reading fault / fallback');
 if(s.tank_sensor_ok===false)rows.push('TEMP02 [INFO] Control tank reading unavailable');
 (s.water||[]).forEach(p=>{if(p.sensor_ok===false)rows.push('TEMP03 [ACTIVE] Water probe '+p.index+' stale')});
 return rows;
}
function diagnosticReport(s){
 const fields=['fan_diag','burn_state_text','safety_text','fan_demand','fan_final','exhaust_raw','exhaust_smooth','tank_temp','adaptive_learning','adaptive_slope','adaptive_learning_status','exhaust_sensor_ok','tank_sensor_ok','wifi_ok','rssi','loop_last_ms','loop_max_ms','network_last_ms','network_max_ms'];
 return ['Boiler Assistant 3.4 diagnostics',new Date().toISOString(),'Software observations only; not fan-speed or mains-wiring verification.',...diagnosticCodes(s),...(s?fields.filter(k=>Object.prototype.hasOwnProperty.call(s,k)).map(k=>k+': '+s[k]):[])].join('\n');
}
function renderExhaust(s){
 const gauge=document.getElementById('exhaustGauge'),value=s.exhaust_smooth,target=s.exhaust_setpoint,band=s.deadband;
 const valid=Number.isFinite(value)&&s.exhaust_sensor_ok!==false&&!s.exhaust_fallback,configured=Number.isFinite(target)&&target>0&&Number.isFinite(band)&&band>0;
 const low=target-band/2,high=target+band/2,position=valid?Math.max(0,Math.min(1,(value-100)/800)):0;
 const status=!configured?'Band unavailable':value<low?'Below band':value>high?'Above band':'In band';
 const color=!valid||!configured?'#68767a':value<low?'#f6c65b':value>high?'var(--danger)':'var(--good)';
 gauge.classList.toggle('invalid',!valid);gauge.style.setProperty('--needle',(position*270-135)+'deg');gauge.style.setProperty('--dial-color',color);
 document.getElementById('boilerArt').style.setProperty('--smoke-color',color);
 const edges=configured?[0,Math.max(0,Math.min(75,(low-100)/800*75)),Math.max(0,Math.min(75,(high-100)/800*75)),75]:[0,0,0,75];
 ['exhaustLow','exhaustBand','exhaustHigh'].forEach((id,i)=>{
  const zone=document.getElementById(id);
  zone.setAttribute('visibility',configured?'visible':'hidden');
  zone.setAttribute('stroke-dasharray',(edges[i+1]-edges[i])+' '+(100-edges[i+1]+edges[i]));
  zone.setAttribute('stroke-dashoffset',-edges[i]);
 });
 const scaleStatus=valid?(value<100?' / Below scale':value>900?' / Above scale':''):'';
 text('exhaust',valid?Math.round(value)+' F':'---');
 gauge.setAttribute('aria-label',valid?'Exhaust '+Math.round(value)+' F, '+status+scaleStatus+
  ', scale 100 to 900 F'+(configured?', deadband '+low+' to '+high+' F':''):'Exhaust temperature unavailable');
}
async function state(){try{const s=await requestJson('/api/state');if(s.error)throw new Error(s.error);lastState=s;lastStateMs=Date.now();renderSelfClean(s);renderFanDiagnostics(s);renderExhaust(s);text('pushStatus','Last push delivery: '+(s.push_status||'Idle'));
 const fanValue=Number(s.fan_final??s.fan??0);
 const fanOff=s.burn_state_text==='IDLE'||fanValue<=0;
 const exhaustValue=s.exhaust_sensor_ok!==false&&!s.exhaust_fallback?s.exhaust_smooth:null;
 const metric=s.env?.units==='metric';
 const outdoor=s.env?.temp_f===null?'ERR':(metric?Math.round((s.env?.temp_f-32)*5/9)+' C':Math.round(s.env?.temp_f)+' F');
 const humidity=s.env?.humidity===null?'ERR':Math.round(s.env?.humidity)+'%';
 const pressure=s.env?.pressure===null?'ERR':(metric?(Number(s.env?.pressure)/10).toFixed(1)+' kPa':(Number(s.env?.pressure)*0.02953).toFixed(2)+' inHg');
 const fanIcon=document.getElementById('fanIcon');
 const brandMark=document.getElementById('brandMark');
 const brandTemp=document.getElementById('brandTemp');
 const burnState=s.burn_state_text||s.burn_state;
 const isIdle=burnState==='IDLE';
 text('connection','Online');
 text('burnState',burnState);
 text('brandTemp',Number.isFinite(exhaustValue)?'Exhaust '+Math.round(exhaustValue)+' F':'Exhaust ---');
 brandMark.classList.toggle('idle',isIdle);
 brandMark.classList.toggle('burning',!isIdle&&burnState!=='EMBER_GUARD');
 text('fan',fanOff?'0%':fanValue+'%');
 text('adaptiveSlope',Number(s.adaptive_slope??1).toFixed(3));
 text('tank',s.tank_temp===null?'ERR':Math.round(s.tank_temp)+' F');
 text('tankLimits',(s.tank_high??'---')+' / '+(s.tank_low??'---')+' F');
 text('outdoor',outdoor);
 text('humidity',humidity);
 text('pressure',pressure);
 text('mainOutdoor',outdoor);
 text('mainHumidity',humidity);
 text('mainPressure',pressure);
 text('exhaustOk',s.exhaust_sensor_ok?'OK':'FAULT');
 text('tankOk',s.tank_sensor_ok?'OK':'FAULT');
 text('wifi',s.wifi_ok?'OK':'OFF');
 fanIcon.classList.toggle('running',!fanOff);
 document.getElementById('fanGauge').style.setProperty('--gauge',fanValue+'%');
 renderProbes(s.water);
 showAlert(s.alert||'');

 if(s.damper_delay_active)text('burnState','DAMPER DELAY');
 sampleLiveTrend(s);
 }catch(e){renderFanDiagnostics(null);lastStateMs=0;throw e}}
function initSelfCleanTimeOptions(){['self_clean_start_hour','self_clean_end_hour'].forEach(id=>{const select=document.getElementById(id);if(!select)return;if(select.options.length>0)return;for(let hour=0;hour<24;hour++){const suffix=hour<12?'AM':'PM';const displayHour=hour%12||12;select.add(new Option(displayHour+' '+suffix,hour))}})}
async function settings(){initSelfCleanTimeOptions();const s=await requestJson('/api/settings');ids.forEach(k=>set(k,s[k]));const zone=document.getElementById('self_clean_timezone');if(zone){const zv=String(s.self_clean_utc_offset)+'|'+(s.self_clean_dst?1:0);for(let i=0;i<zone.options.length;i++){if(zone.options[i].value===zv){zone.value=zv;break}}}lastSettingsMs=Date.now();const shareYes=document.querySelector('#share_data option[value="1"]');if(shareYes)shareYes.disabled=!s.sd_ready&&!s.share_data;const shareStatus=document.getElementById('share_status');if(shareStatus)shareStatus.textContent=s.share_active?'Sharing is on':s.share_data?'Paused: no working SD card':s.sd_ready?'Sharing is off':'Fit an SD card to allow sharing';if(lastState)renderSelfClean(lastState)}
function renderSelfClean(s){const panel=document.getElementById('selfCleanPanel');const value=document.getElementById('selfCleanStatus');const count=document.getElementById('selfCleanCount');const button=document.getElementById('selfCleanNow');if(!panel||!value)return;panel.classList.toggle('due',!!s.self_clean_due||!!s.self_clean_manual);panel.classList.toggle('active',!!s.self_clean_active);if(button)button.disabled=!s.self_clean_enabled;value.textContent=!s.self_clean_enabled?'Disabled':s.self_clean_active?'ACTIVE - 100% FAN':s.self_clean_manual?'MANUAL: NEXT BURN':s.self_clean_due?'DUE - WAITING FOR NIGHT BURN':'Enabled';if(count){if(!s.self_clean_enabled){count.textContent=''}else{const interval=Number(s.self_clean_interval||0);const burns=Number(s.self_clean_burn_count||0);const remaining=Math.max(0,interval-burns);count.textContent=s.self_clean_active?'Self-clean running':remaining===0?'Self-clean due':(remaining===1?'Next clean in 1 burn':'Next clean in '+remaining+' burns')}}}
async function selfCleanNow(){try{const j=await requestJson('/api/set',{method:'POST',headers:{'Content-Type':'application/json','X-Boiler-Token':token()},body:JSON.stringify({self_clean_run_now:true})});text('message',j.ok?'Self-clean queued for the next burn':(j.error||'Self-clean request rejected'));await state()}catch(error){text('message',error.status===401?'Wrong or missing password':error.message);reportRequestError(error)}}
async function save(){
 const message=document.getElementById('message');
 message.textContent='';
 const body={};ids.forEach(k=>{const value=document.getElementById(k).value;if(k==='self_clean_timezone'){const parts=value.split('|');body.self_clean_utc_offset=Number(parts[0]);body.self_clean_dst=Number(parts[1])}else if(k==='push_topic'||k==='display_name'||k==='boiler_make'||k==='boiler_model'){body[k]=value}else if(k==='adaptive_learning'){body[k]=Number(value)!==0}else body[k]=Number(value)});
 if(body.boost_fan_pct>body.clamp_max){
  message.textContent='Not saved: Boost fan cannot exceed Fan maximum ('+body.clamp_max+'%).';
  return;
 }
 if(body.boost_fan_pct<body.clamp_min){
  message.textContent='Not saved: Boost fan cannot be below Fan minimum ('+body.clamp_min+'%).';
  return;
 }
 const entered=prompt('Enter the Control/API Password to confirm saving settings');
 if(entered===null||entered===''){message.textContent='Save cancelled';return}
 localStorage.setItem(tokenKey,entered);
 let j;
 try{j=await requestJson('/api/set',{method:'POST',headers:{'Content-Type':'application/json','X-Boiler-Token':entered},body:JSON.stringify(body)})}
 catch(error){message.textContent='Save not confirmed: '+error.message;throw error}
 if(!j.ok){message.textContent='Not saved: '+(j.error||'Rejected');return}
 message.textContent='Saved';
 await settings();await state();
}
async function pushTest(){let t=token();if(!t){const v=prompt('Enter the Control/API Password to send a test notification');if(v===null||v==='')return;localStorage.setItem(tokenKey,v);t=v}const j=await requestJson('/api/push_test',{method:'POST',headers:{'X-Boiler-Token':t}});text('message',j.ok?'Test queued - see delivery status below':(j.error||'Push failed'));await state()}
async function resetAdaptiveLearning(){const entered=prompt('Enter the Control/API Password to reset learned fan tuning');if(entered===null||entered==='')return;const j=await requestJson('/api/set',{method:'POST',headers:{'Content-Type':'application/json','X-Boiler-Token':entered},body:JSON.stringify({reset_adaptive_learning:true})});text('message',j.ok?'Fan learning reset: gain 1.000, lookahead 15 s':(j.error||'Reset rejected'));await settings();await state()}
function placeNavigation(){const nav=document.querySelector('main nav');const id=activeTab('settings')?'settings':activeTab('history')?'history':null;if(id)document.getElementById(id).before(nav);else document.querySelector('main').appendChild(nav)}
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));document.querySelectorAll('section').forEach(x=>x.classList.remove('active'));b.classList.add('active');document.getElementById(b.dataset.target).classList.add('active');placeNavigation();renderHistorySnapshot();if(lastState)renderProbes(lastState.water);pollNow()});
const pushStatus=document.createElement('div');pushStatus.id='pushStatus';pushStatus.className='hint';document.getElementById('pushTest').after(pushStatus);
const fanDiagnostics=document.createElement('div');fanDiagnostics.id='diagnostics';fanDiagnostics.innerHTML='<div class="card"><h2>Diagnostic Test</h2><button id="copyDiagnostics">Copy Report</button><p id="diagnosticCopyStatus" role="status"></p><textarea id="diagnosticReport" readonly aria-label="Diagnostic report" style="width:100%;min-height:180px"></textarea></div>';document.querySelector('main').insertBefore(fanDiagnostics,document.querySelector('main nav'));
function prepareDiagnosticReport(){
 const report=document.getElementById('diagnosticReport');
 report.value=healthSummary+'\n'+diagnosticReport(Date.now()-lastStateMs<=10000?lastState:null);
 report.value+='\n'+healthLines(lastStateMs&&Date.now()-lastStateMs<=10000?lastState:null).join('\n');
 report.value+='\n'+healthSettings;
 return report;
}
document.getElementById('copyDiagnostics').onclick=async()=>{
 const report=document.getElementById('diagnosticReport');
 if(!report.value||document.getElementById('runHealthCheck').disabled){text('diagnosticCopyStatus','Run the test and wait for it to finish.');return}
 try{
  if(!navigator.clipboard?.writeText)throw new Error('Clipboard unavailable on this connection');
  await navigator.clipboard.writeText(report.value);
  text('diagnosticCopyStatus','Report copied');
 }catch(e){report.focus();report.select();text('diagnosticCopyStatus','Automatic copy failed. Copy the selected report manually.')}
};
const saveDiagnostics=document.createElement('button');saveDiagnostics.textContent='Save Report';document.getElementById('copyDiagnostics').after(saveDiagnostics);
function saveDiagnosticReport(){
 const report=document.getElementById('diagnosticReport'),link=document.createElement('a');
 if(!report.value||document.getElementById('runHealthCheck').disabled){text('diagnosticCopyStatus','Run the test and wait for it to finish.');return}
 let url;
 try{
  url=URL.createObjectURL(new Blob([report.value],{type:'text/plain;charset=utf-8'}));
  link.href=url;link.download='boiler-diagnostics-'+new Date().toISOString().replace(/[:.]/g,'-')+'.txt';
  document.body.appendChild(link);link.click();
  text('diagnosticCopyStatus','Download requested. Check Downloads or your browser save/share menu.');
 }catch(e){report.focus();report.select();text('diagnosticCopyStatus','Download failed. Copy the selected report manually.')}
 finally{link.remove();if(url)setTimeout(()=>URL.revokeObjectURL(url),60000)}
}
saveDiagnostics.onclick=saveDiagnosticReport;
setInterval(()=>{updateConnectionStatus();pollWatchdog();if(Date.now()-lastStateMs>10000)renderFanDiagnostics(null)},1000);
document.getElementById('save').onclick=runAction(save);document.getElementById('resetAdaptiveLearning').onclick=runAction(resetAdaptiveLearning);document.getElementById('pushTest').onclick=runAction(pushTest);document.getElementById('selfCleanNow').onclick=runAction(selfCleanNow);document.getElementById('resetAlarm').onclick=runAction(resetAlarm);document.getElementById('alertReset').onclick=runAction(resetAlarm);document.getElementById('stateReset').onclick=runAction(resetAlarm);document.getElementById('token').onclick=()=>{const v=prompt('Enter the Control/API Password saved during WiFi setup');if(v!==null)localStorage.setItem(tokenKey,v)};setInterval(syncBoilerArt,500);setInterval(syncStateAlarm,500);
</script>
)rawliteral"
#include "PerformanceDashboard.h"
#include "SdLogDashboard.h"
#include "HistoryDashboard.h"
#include "SeasonalDashboard.h"
#include "NetworkDashboard.h"
#include "WaterCalibrationDashboard.h"
#include "HealthDashboard.h"
#include "WhatsNewDashboard.h"
R"rawliteral(
<script>pollNow();</script>
</body></html>
)rawliteral";

#endif
