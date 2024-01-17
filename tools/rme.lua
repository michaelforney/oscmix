-- RME dissector
local endpoint_field = Field.new('usb.endpoint_address.number')
local value_field = ProtoField.uint16('rme.value', 'Value', base.HEX)
local register_field = ProtoField.uint16('rme.register', 'Register', base.HEX)

local function format_bool(val)
	val = val:le_uint()
	if val == 0 then
		return 'off'
	elseif val == 1 then
		return 'on'
	end
end

local function format_int(val)
	return val:le_int()
end

local function format_int10(val)
	val = val:le_int()
	return val / 10
end

local function format_int100(val)
	val = val:le_int()
	return val / 100
end

local function format_time(val)
	val = val:le_uint()
	return string.format('%.2d:%.2d', bit32.rshift(val, 8), bit32.band(val, 0xff))
end

local function format_date(val)
	val = val:le_uint()
	return string.format('%.4d-%.2d-%.2d', 2000 + bit32.rshift(val, 9), bit32.band(bit32.rshift(val, 5), 0xf), bit32.band(val, 0x1f))
end

local function format_volume(val)
	val = val:le_uint()
	local ref
	if bit32.band(val, 0x8000) ~= 0 then
		ref = 0x1000
		val = bit32.band(val, 0x7fff)
	else
		ref = 0x8000
	end
	val = (bit32.bxor(val, 0x4000) - 0x4000) / ref
	local phase
	if val < 0 then
		phase = ' Phase Inverted'
		val = -val
	else
		phase = ''
	end
	return string.format('%.2f dB%s', 20 * math.log10(val), phase)
end

local roomtypes = {
	'Small Room',
	'Medium Room',
	'Large Room',
	'Walls',
	'Shorty',
	'Attack',
	'Swagger',
	'Old School',
	'Echoistic',
	'8plus9',
	'Grand Wide',
	'Thicker',
	'Envelope',
	'Gated',
	'Space',
}
local function format_roomtype(val)
	return roomtypes[val:le_int()]
end

local function format_enum(enum)
	return function(val)
		val = val:le_uint()
		if val < #enum then
			val = val + 1
		end
		return enum[val]
	end
end

local function format_cue(val)
	if val:le_int() == 0xffff then
		return 'No Cue'
	end
	local to = val(0, 1):le_int() + 1
	local from = val(1):le_int() + 1
	return string.format('%d to %d', from, to)
end

local function format_controlroom(val)
	local bits = {}
	if val:bitfield(7) == 1 then table.insert(bits, 'Main Mono') end
	if val:bitfield(4) == 1 then table.insert(bits, 'Ext. Input') end
	if val:bitfield(3) == 1 then table.insert(bits, 'Talkback') end
	if val:bitfield(2) == 1 then table.insert(bits, 'Speaker B') end
	if val:bitfield(1) == 1 then table.insert(bits, 'Dim Enabled') end
	return table.concat(bits, ', ')
end

local bandtypes = {'Peak', 'Shelf', 'High Cut'}

local dspfields = {
	[0x0c] = {name='Low Cut Enable', format=format_bool},
	[0x0d] = {name='Low Cut Freq', format=format_int},
	[0x0e] = {name='Low Cut dB/oct', format=format_enum{6, 12, 18, 24}},
	[0x0f] = {name='Eq Enable', format=format_bool},
	[0x10] = {name='Eq Band 1 Type', format=format_enum(bandtypes)},
	[0x11] = {name='Eq Band 1 Gain', format=format_int10},
	[0x12] = {name='Eq Band 1 Freq', format=format_int},
	[0x13] = {name='Eq Band 1 Q', format=format_int10},
	[0x14] = {name='Eq Band 2 Gain', format=format_int10},
	[0x15] = {name='Eq Band 2 Freq', format=format_int},
	[0x16] = {name='Eq Band 2 Q', format=format_int10},
	[0x17] = {name='Eq Band 3 Type', format=format_enum(bandtypes)},
	[0x18] = {name='Eq Band 3 Gain', format=format_int10},
	[0x19] = {name='Eq Band 3 Freq', format=format_int},
	[0x1a] = {name='Eq Band 3 Q', format=format_int10},
	[0x1b] = {name='Dynamics Enable', format=format_bool},
	[0x1c] = {name='Dynamics Gain', format=format_int10},
	[0x1d] = {name='Dynamics Attack', format=format_int},
	[0x1e] = {name='Dynamics Release', format=format_int},
	[0x1f] = {name='Dynamics Comp. Threshold', format=format_int10},
	[0x20] = {name='Dynamics Comp. Ratio', format=format_int10},
	[0x21] = {name='Dynamics Exp. Threshold', format=format_int10},
	[0x22] = {name='Dynamics Exp. Ratio', format=format_int10},
	[0x23] = {name='Autolevel Enable', format=format_bool},
	[0x24] = {name='Autolevel Max Gain', format=format_int10},
	[0x25] = {name='Autolevel Headroom', format=format_int10},
	[0x26] = {name='Autolevel Rise Time', format=format_int10},
}
local input_fields = setmetatable({
	[0x00] = {name='Mute', format=format_bool},
	[0x01] = {name='FX Send', format=format_int10},
	[0x02] = {name='Stereo', format=format_bool},
	[0x03] = {name='Record', format=format_bool},
	[0x05] = {name='Play Channel', format=format_int},
	[0x06] = {name='M/S Proc', format=format_bool},
	[0x07] = {name='Phase Invert', format=format_bool},
	[0x08] = {name='Gain', format=format_int10},
	[0x09] = {name='48v', format=format_bool},
	[0x0a] = {name='Autoset', format=format_bool},
	[0x0b] = {name='Hi-Z', format=format_bool},
}, {__index=dspfields})
local output_fields = setmetatable({
	[0x00] = {name='Volume', format=format_int10},
	[0x01] = {name='Balance', format=format_int},
	[0x02] = {name='Mute', format=format_bool},
	[0x03] = {name='FX Return', format=format_int10},
	[0x04] = {name='Stereo', format=format_bool},
	[0x05] = {name='Record', format=format_bool},
	[0x07] = {name='Play Channel', format=format_int},
	[0x08] = {name='Phase Invert', format=format_bool},
	[0x09] = {name='Ref. Level', format=format_enum{'+4dBu', '+13dBu', '+19dBu'}},
}, {__index=dspfields})
local global_fields = {
	[0x3000] = {name='Reverb Enable', format=format_bool},
	[0x3001] = {name='Reverb Room Type', format=format_enum(roomtypes)},
	[0x3002] = {name='Reverb Pre Delay', format=format_int},
	[0x3003] = {name='Reverb Low Cut Freq', format=format_int},
	[0x3004] = {name='Reverb Room Scale', format=format_int100},
	[0x3005] = {name='Reverb Attack Time', format=format_int},
	[0x3006] = {name='Reverb Hold Time', format=format_int},
	[0x3007] = {name='Reverb Release Time', format=format_int},
	[0x3008] = {name='Reverb High Cut Freq', format=format_int},
	[0x3009] = {name='Reverb Time', format=format_int10},
	[0x300a] = {name='Reverb High Damp', format=int},
	[0x300b] = {name='Reverb Smoothness', format=format_int},
	[0x300c] = {name='Reverb Volume', format=format_int10},
	[0x300d] = {name='Reverb Stereo Width', format=format_int100},
	[0x3014] = {name='Echo Enable', format=format_bool},
	[0x3015] = {name='Echo Type', format=format_enum{'Stereo Echo', 'Stereo Cross', 'Pong Echo'}},
	[0x3016] = {name='Echo Delay Time', format=format_int1000},
	[0x3017] = {name='Echo Feedback', format=format_int},
	[0x3018] = {name='Echo High Cut', format=format_enum{'off', '16kHz', '12kHz', '8kHz', '4kHz', '2kHz'}},
	[0x3019] = {name='Echo Volume', format=format_int10},
	[0x301a] = {name='Echo Stereo Width', format=format_int100},
	[0x3050] = {name='Main Out', format=format_enum{'1/2', '3/4', '5/6', '7/8', '9/10', '11/12', '13/14', '15/16', '17/18', '19/20'}},
	[0x3051] = {name='Main Mono', format=format_bool},
	[0x3053] = {name='Mute Enable', format=format_bool},
	[0x3055] = {name='Dim', format=format_bool},
	[0x3064] = {name='Clock Source', format=format_enum{'Internal', 'Word Clock', 'SPDIF', 'AES', 'Optical'}},
	[0x3066] = {name='Word Clock Out', format=format_bool},
	[0x3067] = {name='Word Clock Out Single Speed', format=format_bool},
	[0x3068] = {name='Word Clock Termination', format=format_bool},
	[0x3078] = {name='Optical Out', format=format_enum{'ADAT', 'SPDIF'}},
	[0x3079] = {name='SPDIF Format', format=format_enum{'Consumer', 'Professional'}},
	[0x3e00] = {name='Cue', format=format_cue},
	[0x3e02] = {name='Control Room Status', format=format_controlroom},
	[0x3e08] = {name='Time', format=format_time},
	[0x3e09] = {name='Date', format=format_date},
	[0x3e9a] = {name='Durec Play Control', format=format_enum{[0x8120]='Stop Record', [0x8121]='Stop', [0x8122]='Record', [0x8123]='Play/Pause'}},
	[0x3e9b] = {name='Durec Delete', format=format_enum{[0x8000]='Delete'}},
	[0x3e9d] = {name='Durec Seek', format=format_int},
	[0x3e9e] = {name='Durec Track Select', format=format_enum{'Previous', 'Next'}},
	[0x3ea0] = {name='Durec Play Mode', format=format_enum{[0x8000]='Single', [0x8001]='UFX Single', [0x8002]='Continuous', [0x8003]='Single Next', [0x8004]='Repeat Single', [0x8005]='Repeat All'}},
}
rme_proto = Proto('rme', 'RME USB Protocol')
rme_proto.fields = {value_field, register_field}

local function format_input(reg, val)
	local chan = math.floor(reg / 0x40) + 1
	reg = reg % 0x40
	local field = input_fields[reg]
	if field then
		return string.format('Input %d %s', chan, field.name), field.format(val)
	else
		return string.format('Input %d %#.2x', chan, reg)
	end
end

local function format_output(reg, val)
	reg = reg - 0x500
	local chan = math.floor(reg / 0x40) + 1
	reg = reg % 0x40
	local field = output_fields[reg]
	if field then
		return string.format('Output %d %s', chan, field.name), field.format(val)
	else
		return string.format('Output %d %#.2x', chan, reg)
	end
end


local function format_global(reg, val)
	local field = global_fields[reg]
	if field then
		return field.name, field.format(val)
	end
end

local function format_mixlabel(reg, val)
	reg = reg - 0x2000
	val = val:le_uint()
	local mix = math.floor(reg / 0x40) + 1
	local chan = reg % 0x40 + 1
	local regdesc = string.format('Mix %d, Input %d Label', mix, chan)
	local pan = bit32.band(val, 0x8000) == 0
	val = bit32.bxor(bit32.band(val, 0x7fff), 0x4000) - 0x4000
	if pan then
		valdesc = string.format('%.2f dB', val / 10)
	else
		valdesc = string.format('Pan %d', val)
	end
	return regdesc, valdesc
end

local function format_mixvolume(reg, val)
	reg = reg - 0x4000
	local mix = math.floor(reg / 0x40) + 1
	local chan = reg % 0x40 + 1
	local desc = bit32.band(chan, 0x20) == 0 and 'Input' or 'Playback'
	chan = bit32.band(chan, 0x1f)
	local regdesc = string.format('Mix %d, %s %d Volume', mix, desc, chan)
	return regdesc, format_volume(val)
end

local function format_playbackfx(reg, val)
	local chan, side
	if reg < 0x47e0 then
		chan = (reg - 0x47a0)
		side = 'Left'
	else
		chan = (reg - 0x47e0)
		side = 'Right'
	end
	return string.format('Playback %d FX Send %s', chan, side), format_volume(val)
end

local function format_channame(reg, val)
	reg = reg - 0x3200
	local chan = math.floor(reg / 0x08) + 1
	reg = reg % 0x08
	val = val:le_uint()
	local type
	if chan > 20 then
		type = 'Output'
		chan = chan - 20
	else
		type = 'Input'
	end
	local regdesc = string.format('%s %d Name[%d:%d]', type, chan, reg * 2, reg * 2 + 1)
	local valdesc = string.char(bit32.band(val, 0xff), bit32.rshift(val, 8))
	return regdesc, valdesc
end

function rme_proto.dissector(buffer, pinfo, tree)
	--local ep = endpoint_field().value
	--if ep ~= 12 then return 0 end
	pinfo.cols.protocol = rme_proto.name
	local subtree = tree:add(rme_proto, buffer(), 'RME Protocol Data')
	local length = buffer:len()
	local i = 0
	while i + 4 <= length do
		local subsubtree = subtree:add(rme_proto, buffer(i, 4), 'Set Register')
		local valbuf = buffer(i, 2)
		local regbuf = buffer(i + 2, 2)
		local val = valbuf:le_uint()
		local reg = bit32.band(regbuf:le_uint(), 0x7fff)
		local format
		local regdesc, valdesc
		if reg < 0x0500 then
			format = format_input
		elseif reg < 0x0a00 then
			format = format_output
		elseif reg >= 0x2000 and reg < 0x2500 then
			format = format_mixlabel
		elseif reg >= 0x3200 and reg < 0x3340 then
			format = format_channame
		elseif reg >= 0x4000 and reg < 0x4500 then
			format = format_mixvolume
		elseif reg >= 0x4700 and reg < 0x4800 then
			format = format_playbackfx
		else
			format = format_global
		end
		local regdesc, valdesc
		if format ~= nil then
			regdesc, valdesc = format(reg, valbuf)
		end
		if regdesc then regdesc = string.format('(%s)', regdesc) end
		if valdesc then valdesc = string.format('(%s)', valdesc) end
		subsubtree:add_le(register_field, regbuf, reg, nil, regdesc)
		subsubtree:add_le(value_field, valbuf, val, nil, valdesc)
		i = i + 4
	end
end

local usb_table = DissectorTable.get('usb.product')
usb_table:add(0x2a393f82, rme_proto)
--print(usb_table:get_dissector(0x2a393f82))
--usb_table = DissectorTable.get('usb.bulk')
--usb_table:add(0xffff, rme_proto)

local peak_field = ProtoField.uint64('rme.peak', 'Peak', base.HEX)
local rms_field = ProtoField.uint32('rme.value', 'RMS', base.HEX)

rme_levels_proto = Proto('rme_levels', 'RME Levels')
rme_levels_proto.fields = {peak_field, rms_field}

function rme_levels_proto.dissector(buffer, pinfo, tree)
	assert(buffer:len() % 12 == 0)
	for i = 0, buffer:len() - 12, 12 do
		tree:add_le(peak_field, buffer(i, 8))
		tree:add_le(rms_field, buffer(i + 8, 4))
	end
end

-- RME SysEx dissector
local sysex_rme_proto = Proto('sysex_rme', 'RME SysEx Protocol')
local devid_field = ProtoField.uint8('sysex_rme.devid', 'Device ID', base.HEX)
local subid_field = ProtoField.uint8('sysex_rme.subid', 'Sub ID', base.HEX)
sysex_rme_proto.fields = {devid_field, subid_field}

local function sysex_decode(input)
	local output = ByteArray.new()
	output:set_size(math.floor((input:len() * 7) / 8))
	local byte = 0
	local j = 0
	for i = 0, input:len() - 1 do
		byte = bit32.bor(bit32.lshift(input:get_index(i), -i % 8), byte)
		if i % 8 ~= 0 then
			output:set_index(j, bit32.band(byte, 0xff))
			byte = bit32.rshift(byte, 8)
			j = j + 1
		end
	end
	return output
end

function sysex_rme_proto.dissector(buffer, pinfo, tree)
	pinfo.cols.protocol = sysex_rme_proto.name
	local subtree = tree:add(sysex_rme_proto, buffer(), 'RME SysEx Protocol')
	local subid = buffer(1, 1)
	subtree:add(devid_field, buffer(0, 1))
	subtree:add(subid_field, subid)
	buffer = buffer(2)

	local decoded_body = ByteArray.new()
	for i = 0, buffer:len() - 5, 5 do
		decoded_body = decoded_body..sysex_decode(buffer(i, 5):bytes())
	end

	buffer = decoded_body:tvb()
	if subid:le_uint() == 0 then
		rme_proto.dissector(buffer, pinfo, tree)
	else
		rme_levels_proto.dissector(buffer, pinfo, tree)
	end
	--[[
	buffer = decoded_body:tvb()
	for i = 0, buffer:len() - 1, 4 do
		local valbuf = buffer(i, 2)
		local regbuf = buffer(i + 2, 2)
		local reg = bit32.band(regbuf:le_uint(), 0x7fff)
		local subtree = tree:add(sysex_rme_proto, buffer(i, 4), 'Set Register')
		subtree:add_le(register_field, regbuf, reg)
		subtree:add_le(value_field, valbuf)
	end
	--tree:add(body_field, buffer(2))
	tree:add(body_field, buffer())
	--tree:add(tvb, 'Body')
	--]]
end

local sysex_table = DissectorTable.get('sysex.manufacturer')
sysex_table:add(0x00200d, sysex_rme_proto)
