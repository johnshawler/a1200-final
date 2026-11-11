        section music,data_c            ; chip RAM: the sound chip plays from here
        xdef    _mod_data,_fx_data
        cnop    0,4
_mod_data:  incbin  "examples/test.mod" ; a tiny ProTracker module made for this demo
        cnop    0,4
_fx_data:   incbin  "examples/fx.raw"   ; 400 bytes, signed 8-bit, the first two bytes are zero
        cnop    0,4
