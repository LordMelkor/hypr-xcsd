-- Signal/state regression checks against the actual installed Lua module.
local subject = assert(arg[1], "pass keep-below-bar.lua path")
local timers, events, releases, moves, w, cursor, active
local function setup(native)
  timers, events, releases, moves = {}, {}, {}, 0
  active = true
  cursor = { x = 200, y = 10 }
  w = {address='123',floating=true,mapped=true,hidden=false,fullscreen=0,
    tags={'omarchy-floating-workspace','x11-csd'},at={x=100,y=0},size={x=500,y=300},
    monitor={y=0,reserved={top=26}},workspace={visible=true},xwayland=true}
  hl = {
    plugin = native and {xcsd={drag_active=function() return active end}} or {},
    get_config=function() return 30 end,
    get_cursor_pos=function() return cursor end,
    get_windows=function() return {w} end,
    dsp={window={move=function(v) return v end}},
    dispatch=function(v) moves=moves+1;w.at.y=v.y end,
    timer=function(fn,opts) table.insert(timers,{fn=fn,ms=opts.timeout}) end,
    on=function(event,fn) events[event]=fn end,
    bind=function(_,fn) table.insert(releases,fn) end,
  }
  dofile(subject)
end
local function run(ms)
  for i,t in ipairs(timers) do
    if t.ms==ms then table.remove(timers,i);t.fn();return end
  end
  error('missing timer '..ms)
end
setup(true)
run(100)
assert(moves==0, 'never move a window during native drag')
events['window.active'](w);run(1)
assert(moves==0, 'focus at drag start is not drag end')
releases[1]();run(1)
assert(moves==0, 'release while another native drag remains active is not safe')
active=false
events['window.active'](w);run(1)
assert(moves==1 and w.at.y==26, 'correct after native drag end with stationary cursor')
setup(false)
run(100)
assert(moves==0, 'no getter: conservatively spare cursor window')
events['window.active'](w);run(1)
assert(moves==0)
cursor={x=1000,y=1000};run(500)
assert(moves==1 and w.at.y==26)
for _,field in ipairs({'hidden','fullscreen','invisible'}) do
  setup(true)
  if field=='hidden' then w.hidden=true elseif field=='fullscreen' then w.fullscreen=1 else w.workspace.visible=false end
  run(100)
  assert(moves==0 and #timers==0, 'ineligible window must not keep polling: '..field)
end
setup(true)
events['layer.opened']();events['layer.closed']();events['workspace.active']()
assert(#timers==1, 'coalesce structural event bursts')
print('bar correction: 9 state/signal and scheduling assertions passed')
