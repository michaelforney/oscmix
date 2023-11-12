local function leveltoval(level)
	if level < 0.5 then
		level = level * 0x8000
	else
		level = level * 0x1000 + 0x8000
	end
	return string.format('%x', math.floor(level))
end

function dbtoval(db, scale)
	return leveltoval(10 ^ (db / 20) * (scale or 1))
end

function valtodb(x)
	local ref
	local phase = false
	ref = x & 0x8000 ~= 0 and 0x1000 or 0x8000
	x = (x & 0x7fff ~ 0x4000) - 0x4000
	if x < 0 then
		x = -x
		phase = true
	end
	return 20 * math.log10(x / ref), phase
end

function stereostereopan(vol, pan, width)
	local level = math.pow(10, vol / 20)
	local level00 = (100 - math.max(pan, 0)) * (1 + width) / 200 * level
	local level01 = (100 + math.min(pan, 0)) * (1 - width) / 200 * level
	local level10 = (100 - math.max(pan, 0)) * (1 - width) / 200 * level
	local level11 = (100 + math.min(pan, 0)) * (1 + width) / 200 * level
	local sqlevel0 = level00^2 + level01^2
	local sqlevel1 = level10^2 + level11^2
	print(2 * level00^2 / sqlevel0 - 1)
	print(10 * math.log10(sqlevel0), math.acos(2 * level00^2 / sqlevel0 - 1) * 200 / math.pi - 100)
	print(10 * math.log10(sqlevel1), math.acos(2 * level10^2 / sqlevel1 - 1) * 200 / math.pi - 100)
	return leveltoval(level00), leveltoval(level01), leveltoval(level10), leveltoval(level11)
end
