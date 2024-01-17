function string.fromhex(str)
	return str:gsub('..', function(hex) return string.char(tonumber(hex, 16)) end)
end

function string.tohex(str)
	return str:gsub('.', function(byte) return string.format('%02x', string.byte(byte)) end)
end

local function decode(input)
	local output = ''
	for i = 1, #input - 4, 5 do
		local val = string.byte(input, i) | string.byte(input, i + 1) << 7
		          | string.byte(input, i + 2) << 14 | string.byte(input, i + 3) << 21
		          | string.byte(input, i + 4) << 28
		if val & ~0xffffffff ~= 0 then
			error('leftover data in byte')
		end
		output = output..string.char(val & 0xff, val >> 8 & 0xff, val >> 16 & 0xff, val >> 24 & 0xff)
	end
	return output
end

local regs = {}
local subs = {}
local ignore = {
	[1<<8|0]=true,
	[1<<8|2]=true,
	[1<<8|3]=true,
	[1<<8|5]=true,
	[1<<8|6]=true,
	[1<<8|8]=true,
	[1<<8|9]=true,
	[1<<8|11]=true,
	[1<<8|12]=true,
	[1<<8|14]=true,
	[1<<8|15]=true,
	[1<<8|17]=true,
	[1<<8|18]=true,
	[1<<8|20]=true,
	[1<<8|21]=true,
	[1<<8|23]=true,

	[2<<8|0]=true,
	[2<<8|1]=true,
	[2<<8|2]=true,

	[3<<8|0]=true,
	[3<<8|1]=true,
	[3<<8|2]=true,
	--[3<<8|3]=true,
	--[3<<8|5]=true,
	[3<<8|36]=true,
	[3<<8|38]=true,
	[3<<8|39]=true,
	[3<<8|41]=true,

	[4<<8|0]=true,
	[4<<8|2]=true,
	[4<<8|3]=true,
	[4<<8|5]=true,
	[4<<8|6]=true,
	[4<<8|8]=true,
	[4<<8|9]=true,
	[4<<8|11]=true,
	[4<<8|12]=true,
	[4<<8|14]=true,
	[4<<8|15]=true,
	[4<<8|17]=true,
	[4<<8|18]=true,
	[4<<8|20]=true,
	[4<<8|21]=true,
	[4<<8|23]=true,
}

local function getle32(b)
	return string.byte(b, 4) << 24 | string.byte(b, 3) << 16 | string.byte(b, 2) << 8 | string.byte(b, 1)
end

local function getle64(b)
	return string.byte(b, 8) << 56 | string.byte(b, 7) << 48 | string.byte(b, 6) << 40 | string.byte(b, 5) << 32
	     | string.byte(b, 4) << 24 | string.byte(b, 3) << 16 | string.byte(b, 2) << 8 | string.byte(b, 1)
end

for line in io.lines() do
	local bytes = line:fromhex()
	if bytes:sub(1, 5) ~= '\xf0\x00\x20\x0d\x10' then
		print('unknown', line)
	else
		local subid = bytes:byte(6)
		local prefix = 'subid='..bytes:byte(6)
		bytes = decode(bytes:sub(7, #bytes - 1))
		if subid == 0 then
			for i = 1, #bytes - 3, 4 do
				local regval = bytes:sub(i, i + 3)
				local val = regval:byte(2) << 8 | regval:byte(1)
				local reg = (regval:byte(4) << 8 | regval:byte(3)) & 0x7fff
				if regs[reg] ~= val then
					if reg ~= 0x3082 then
						print(prefix, string.format('reg=0x%04x val=0x%04x (prev 0x%04x)', reg, val, regs[reg] or 0))
						prefix = ''
					end
					regs[reg] = val
				end
			end
		elseif subid < 5 then
			assert(#bytes % 12 == 0)
			for i = 1, #bytes / 2, 12 do
				print(prefix, string.format('%016x (%08x)', getle64(bytes:sub(i, i + 7)), getle32(bytes:sub(i + 8, i + 11))))
				prefix = ''
			end
			--[[
				local idx = (i - 1) // 4
				local key = subid << 8 | idx
				local val = getle32(bytes:sub(i, i + 3))
				if not ignore[key] and (subs[key] ~= val or (subid == 2 and idx > 2 and idx < 6) or (subid == 3 and idx > 2 and idx < 6)) then
					io.write(prefix)
					io.write(string.format(' %d:%08x', idx, val))
					prefix = ''
					subs[key] = val
				end
			end
			--]]
		::next::
		end
	end
end
