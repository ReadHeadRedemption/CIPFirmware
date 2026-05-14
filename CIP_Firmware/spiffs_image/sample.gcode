G21             ; units = mm
G90             ; absolute coordinates
G28             ; home all axes
G1 Z5 F6000     ; safe travel height
G0 X20 Y50
G1 X20 Y50 ;F1200
G1 X20 Y100 F1200
G1 X20 Y75
G1 X50 Y75 F1200
G1 X50 Y100
G1 X50 Y50
; ---- end sequence ----
G0 X0 Y0
