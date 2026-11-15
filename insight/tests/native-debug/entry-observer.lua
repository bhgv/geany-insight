-- EN: An auto-continuing observer must not override the requested entry stop.
-- RU: Автопродолжение наблюдателя не должно отменять запрошенную остановку на входе.
-- EN: Ignore event and request continuation; the debugger retains explicit entry stops.
-- RU: Игнорирует event и запрашивает продолжение; отладчик сохраняет явные остановки на входе.
return function(event)
    return false
end
