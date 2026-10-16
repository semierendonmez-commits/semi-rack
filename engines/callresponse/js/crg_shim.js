// crg_shim.js — the slice of the Max [v8] / Live API that crg_engine.js uses,
// re-created so the engine runs unmodified inside the plugin (QuickJS).
//
// Loaded BEFORE crg_engine.js. Everything the engine sends out of its outlets
// is queued in __out and drained by the C++ host after each call.

var __out = [];
var __log = [];
var __stateDirty = false;

function outlet(n){
  var a = Array.prototype.slice.call(arguments, 1);
  if(a.length === 1 && Array.isArray(a[0])) a = a[0];
  __out.push([n].concat(a));
}
function post(){
  __log.push(Array.prototype.join.call(arguments, ' '));
  if(__log.length > 200) __log.shift();
}
function arrayfromargs(args){ return Array.prototype.slice.call(args); }
function messnamed(){}          // Companion buses do not exist in the plugin
function embedmessage(){}
function notifyclients(){ __stateDirty = true; }
var autowatch = 0, inlets = 1, outlets = 1, jsarguments = [], messagename = '';

// --- Task: Max's deferred scheduler, pumped by the host's message-thread timer
var __tasks = [];
function Task(fn, obj){
  this.fn = fn; this.obj = obj; this.due = -1; this.interval = 0; this.iterations = 0;
}
Task.prototype.schedule = function(ms){
  this.due = Date.now() + Math.max(0, Number(ms) || 0);
  if(__tasks.indexOf(this) < 0) __tasks.push(this);
};
Task.prototype.repeat = function(n, ms){ this.interval = Number(ms) || this.interval || 20; this.schedule(this.interval); };
Task.prototype.cancel = function(){ this.due = -1; };
Task.prototype.execute = function(){ this.fn.call(this.obj || this); };
Object.defineProperty(Task.prototype, 'running', { get: function(){ return this.due >= 0; } });

function __crgPump(){
  var now = Date.now();
  // Copy: a task may schedule another while we iterate
  var list = __tasks.slice();
  for(var i = 0; i < list.length; i++){
    var t = list[i];
    if(t.due >= 0 && t.due <= now){
      t.due = -1;
      try { t.fn.call(t.obj || t); } catch(e){ post('task error: ' + e.message); }
      if(t.interval > 0 && t.due < 0 && t.iterations === 0) { /* one-shot repeat emulation not needed */ }
    }
  }
}

// --- LiveAPI: just enough of live_set for tempo, transport and time signature.
// The host fills __host from the plugin's play head.
var __host = {
  tempo: 120, is_playing: 0, signature_numerator: 4, signature_denominator: 4,
  loop: 0, loop_length: 0, current_song_time: -1
};
var __liveObservers = [];
function LiveAPI(a, b){
  var cb = (typeof a === 'function') ? a : null;
  this.path = cb ? b : a;
  this.id = (this.path === 'live_set') ? 1 : 0;
  this._cb = cb;
  this.property = '';
  if(cb) __liveObservers.push(this);
}
LiveAPI.prototype.get = function(p){
  if(this.id && Object.prototype.hasOwnProperty.call(__host, p)) return [__host[p]];
  return null;
};
LiveAPI.prototype.set = function(){};
LiveAPI.prototype.call = function(){ return null; };
LiveAPI.prototype.getcount = function(){ return 0; };

function __hostTransport(on){
  on = on ? 1 : 0;
  if(__host.is_playing === on) return;
  __host.is_playing = on;
  for(var i = 0; i < __liveObservers.length; i++){
    var o = __liveObservers[i];
    if(o.property === 'is_playing' && o._cb){
      try { o._cb(['is_playing', on]); } catch(e){ post('observer error: ' + e.message); }
    }
  }
}
