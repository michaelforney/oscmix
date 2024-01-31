/* OSC */
class OSCDecoder {
	constructor(buffer, offset = 0, length = buffer.byteLength) {
		this.buffer = buffer;
		this.offset = offset;
		this.length = length;
		this.textDecoder = new TextDecoder();
	}
	getString() {
		const data = new Uint8Array(this.buffer, this.offset, this.length)
		const end = data.indexOf(0);
		if (end == -1)
			throw new Error('OSC string is not nul-terminated');
		const str = this.textDecoder.decode(data.subarray(0, end));
		const len = (end + 4) & -4;
		this.offset += len;
		this.length -= len;
		return str;
	}
	getInt() {
		const view = new DataView(this.buffer, this.offset, this.length);
		this.offset += 4;
		this.length -= 4;
		return view.getInt32(0);
	}
	getFloat() {
		const view = new DataView(this.buffer, this.offset, this.length);
		this.offset += 4;
		this.length -= 4;
		return view.getFloat32(0);
	}
}

class OSCEncoder {
	constructor() {
		this.buffer = new ArrayBuffer(1024);
		this.offset = 0;
		this.textEncoder = new TextEncoder();
	}
	data() {
		return new Uint8Array(this.buffer, 0, this.offset);
	}
	#ensureSpace(length) {
		while (this.buffer.length - this.offset < length)
			this.buffer.resize(this.buffer.length * 2);
	}
	putString(value) {
		this.#ensureSpace(value.length + 1);
		const data = new Uint8Array(this.buffer, this.offset, value.length);
		const { read } = this.textEncoder.encodeInto(value, data);
		if (read < value.length)
			throw new Error('string contains non-ASCII characters');
		this.offset += (value.length + 4) & -4;
	}
	putInt(value) {
		this.#ensureSpace(4);
		new DataView(this.buffer, this.offset, 4).setInt32(0, value);
		this.offset += 4;
	}
	putFloat(value) {
		this.#ensureSpace(4);
		new DataView(this.buffer, this.offset, 4).setFloat32(0, value);
		this.offset += 4;
	}
};

class Interface {
	constructor() {
		this.methods = new Map();
	}

	#socket;
	#writeOSC;

	set socket(ws) {
		if (this.#socket)
			this.#socket.close();
		this.#socket = ws;
		const icon = document.getElementById('connection-icon');
		delete icon.dataset.state;
		ws.onmessage = async (event) => {
			//console.log(event);
			this.handleOSC(await event.data.arrayBuffer());
		};
		ws.onopen = (event) => {
			console.log(event);
			icon.dataset.state = 'connected';
		};
		ws.onerror = ws.onclose = (event) => {
			console.log(event);
			icon.dataset.state = 'failed';
			this.#socket = null;
		};
		this.#writeOSC = (data) => {
			ws.send(data);
		};
	}

	set midiPorts({input, output}) {
	}

	handleOSC(buffer, offset, length) {
		const decoder = new OSCDecoder(buffer, offset, length);
		const addr = decoder.getString();
		if (addr == '#bundle') {
			decoder.getInt();
			decoder.getInt();
			while (decoder.length > 0) {
				const length = decoder.getInt();
				if (length % 4 != 0)
					throw new Error('OSC bundle has invalid padding');
				this.handleOSC(buffer, decoder.offset, length);
				decoder.offset += length;
				decoder.length -= length;
			}
		} else {
			const types = decoder.getString();
			const args = []
			for (const type of types.substring(1)) {
				switch (type) {
				case 's': args.push(decoder.getString()); break;
				case 'i': args.push(decoder.getInt()); break;
				case 'f': args.push(decoder.getFloat()); break;
				}
			}
			const method = this.methods.get(addr);
			if (method)
				method(args);
			/*
			else
				console.log(addr, args);
			*/
		}
	}

	send(addr, types, args) {
		if (types[0] != ',' || types.length != 1 + args.length)
			throw new Error('invalid OSC type string');
		console.log(addr, types, args);
		const encoder = new OSCEncoder();
		encoder.putString(addr);
		encoder.putString(types);
		for (const [i, arg] of args.entries()) {
			switch (types[1 + i]) {
			case 'i': encoder.putInt(arg); break;
			case 'f': encoder.putFloat(arg); break;
			case 's': encoder.putString(arg); break;
			default: throw new Error(`invalid OSC type '${types[1 + i]}'`);
			}
		}
		const data = encoder.data();
		//console.log('sending', data);
		if (this.#writeOSC)
			this.#writeOSC(data);
	}

	bind(addr, types, obj, prop, eventType) {
		//console.log('bind', addr, types, obj, eventType, prop);
		this.methods.set(addr, (args) => {
			obj[prop] = args[0]
			if (eventType)
				obj.dispatchEvent(new OSCEvent(eventType))
		});
		if (eventType) {
			obj.addEventListener(eventType, (event) => {
				if (!(event instanceof OSCEvent))
					this.send(addr, types, [obj[prop]]);
			});
		}
	}
};

class OSCEvent extends Event {}

const svgNS = 'http://www.w3.org/2000/svg';

class EQBand {
	static PEAK = 0;
	static LOW_SHELF = 1;
	static HIGH_SHELF = 2;
	static LOW_PASS = 3;
	static HIGH_PASS = 4;

	#type = EQBand.PEAK;
	#gain = 0;
	#freq = 100;
	#q = 1;

	constructor() {
		this.#updateCoeffs();
	}

	#updateCoeffs() {
		const f2 = this.#freq * this.#freq;
		const f4 = f2 * f2;
		const A = Math.pow(10, this.#gain / 40);
		const Q = this.#q
		switch (this.#type) {
		case EQBand.PEAK:
			this.a0 = f4;
			this.a1 = (A * A / (Q * Q) - 2) * f2;
			this.a2 = 1;
			this.b0 = f4;
			this.b1 = (1 / (A * A * Q * Q) - 2) * f2;
			break
		case EQBand.LOW_SHELF:
			this.a0 = A * A * f4;
			this.a1 = A * (1 / (Q * Q) - 2) * f2;
			this.a2 = 1;
			this.b0 = f4 / (A * A);
			this.b1 = (1 / (Q * Q) - 2) / A * f2;
			break;
		case EQBand.HIGH_SHELF:
			this.a0 = A * A * f4;
			this.a1 = A * A * A * (1 / (Q * Q) - 2) * f2;
			this.a2 = A * A * A * A;
			this.b0 = A * A * f4;
			this.b1 = A * (1 / (Q * Q) - 2) * f2;
			break;
		case EQBand.LOW_PASS:
			this.a0 = f4;
			this.a1 = 0;
			this.a2 = 0;
			this.b0 = f4;
			this.b1 = (1 / (Q * Q) - 2) * f2;
			break;
		case EQBand.HIGH_PASS:
			this.a0 = 0;
			this.a1 = 0;
			this.a2 = 1;
			this.b0 = f4;
			this.b1 = (1 / (Q * Q) - 2) * f2;
			break;
		}
	}

	set type(value) {
		this.#type = value;
		this.#updateCoeffs();
	}

	set gain(value) {
		this.#gain = value;
		this.#updateCoeffs();
	}

	set freq(value) {
		this.#freq = value;
		this.#updateCoeffs();
	}

	set q(value) {
		this.#q = value;
		this.#updateCoeffs();
	}
}

class Channel {
	static INPUT = 0;
	static OUTPUT = 1;
	static PLAYBACK = 2;

	static #elements = new Set([
		'eq',
		'eq-band1type',
		'eq-band1gain',
		'eq-band1freq',
		'eq-band1q',
		'eq-band2gain',
		'eq-band2freq',
		'eq-band2q',
		'eq-band3type',
		'eq-band3gain',
		'eq-band3freq',
		'eq-band3q',
		'lowcut',
		'lowcut-freq',
		'lowcut-slope',
		'dynamics',
		'dynamics-gain',
		'dynamics-attack',
		'dynamics-release',
		'dynamics-compthres',
		'dynamics-compratio',
		'dynamics-expthres',
		'dynamics-expratio',
		'autolevel',
		'autolevel-maxgain',
		'autolevel-headroom',
		'autolevel-risetime',
	]);

	constructor(type, iface, name, prefix, left, index) {
		const template = document.getElementById('channel-template');
		const fragment = template.content.cloneNode(true);

		this.volumeDiv = fragment.getElementById('channel-volume');

		const nameDiv = fragment.getElementById('channel-name');
		nameDiv.textContent = name;

		this.level = fragment.getElementById('channel-level');
		iface.methods.set(prefix + '/level', (args) => {
			const value = Math.max(args[0], -65);
			if (this.level.value != value)
				this.level.value = value;
		});

		const stereo = fragment.getElementById('channel-stereo');
		if (left) {
			stereo.addEventListener('change', (event) => {
				if (stereo.checked) {
					left.volumeDiv.insertBefore(this.level, left.level.nextSibling);
				} else {
					this.volumeDiv.insertBefore(this.level, this.volumeDiv.firstElementChild);
				}
			});
			fragment.children[0].classList.add('channel-right')
		}
		if (type == Channel.OUTPUT) {
			const selects = document.querySelectorAll('select.channel-volume-output');
			for (const select of selects) {
				const option = new Option(name);
				option.dataset.output = index;
				select.add(option);
			}
			if (left) {
				stereo.addEventListener('change', (event) => {
					const options = document.querySelectorAll('option[data-output="' + index + '"]');
					for (const option of options)
						option.disabled = event.target.checked;
				});
			}
		}
		iface.bind(prefix + '/mute', ',i', fragment.getElementById('channel-mute'), 'checked', 'change');
		switch (type) {
		case Channel.INPUT:
			iface.bind(prefix + '/fxsend', ',i', fragment.getElementById('channel-fx'), 'checked', 'change');
			break;
		case Channel.OUTPUT:
			iface.bind(prefix + '/fxreturn', ',i', fragment.getElementById('channel-fx'), 'checked', 'change');
			break;
		}
		iface.bind(prefix + '/stereo', ',i', stereo, 'checked', 'change');
		// record
		// play channel
		// msproc
		iface.bind(prefix + '/phase', ',i', fragment.getElementById('channel-phase'), 'checked', 'change');
		iface.bind(prefix + '/reflevel', ',i', fragment.getElementById('channel-reflevel'), 'selectedIndex', 'change');
		iface.bind(prefix + '/gain', ',i', fragment.getElementById('channel-gain'), 'value', 'change');
		if (type == Channel.INPUT && (index == 2 || index == 3))
			iface.bind(prefix + '/hi-z', ',i', fragment.getElementById('channel-hi-z'), 'checked', 'change');
		iface.bind(prefix + '/autoset', ',i', fragment.getElementById('channel-autoset'), 'value', 'change');

		const volumeRange = fragment.getElementById('channel-volume-range');
		const volumeNumber = fragment.getElementById('channel-volume-number');
		const output = fragment.getElementById('channel-volume-output');
		if (type == Channel.OUTPUT) {
			volumeRange.oninput = volumeNumber.onchange = (event) => {
				iface.send(prefix + '/volume', ',f', [event.target.value]);
				volumeRange.value = event.target.value;
				volumeNumber.value = event.target.value;
			};
			iface.methods.set(prefix + '/volume', (args) => {
				volumeRange.value = args[0];
				volumeNumber.value = args[0];
			});
			output.remove();
		} else {
			output.addEventListener('change', (event) => {
				volumeRange.value = volumeNumber.value = this.volume[event.target.selectedIndex];
			});
			volumeRange.oninput = volumeNumber.onchange = (event) => {
				iface.send(`/mix/${output.selectedIndex+1}${prefix}`, ',f', [event.target.value]);
				volumeRange.value = volumeNumber.value = event.target.value;
			};
			this.volume = [];
			for (let i = 0; i < 20; ++i) {
				this.volume[i] = -65;
				iface.methods.set(`/mix/${i+1}${prefix}`, (args) => {
					const value = Math.max(args[0], -65);
					this.volume[i] = value;
					if (output.selectedIndex == i) {
						volumeRange.value = value;
						volumeNumber.value = value;
					}
				});
			}
		}

		const onPanelButtonChanged = (event) => {
			for (const label of event.target.parentNode.parentNode.children) {
				const other = label.firstElementChild;
				if (other != event.target)
					other.checked = false;
			}
		};
		for (const node of fragment.querySelectorAll('.channel-panel-buttons input[type="checkbox"]'))
			node.onchange = onPanelButtonChanged;

		const svg = fragment.getElementById('eq-plot');
		const grid = fragment.getElementById('eq-grid');
		const curve = fragment.getElementById('eq-curve');
		this.bands = [new EQBand(), new EQBand(), new EQBand()];
		const observer = new ResizeObserver(() => {this.drawEQ(svg, grid, curve)});
		observer.observe(svg);

		const band1Type = fragment.getElementById('eq-band1type')
		band1Type.addEventListener('change', (event) => {
			this.bands[0].type = EQBand[event.target.value];
			this.drawEQ(svg, grid, curve);
		});
		const band3Type = fragment.getElementById('eq-band3type')
		band3Type.addEventListener('change', (event) => {
			this.bands[2].type = EQBand[event.target.value];
			this.drawEQ(svg, grid, curve);
		});

		for (const [prop, type] of [['gain', ',f'], ['freq', ',i'], ['q', ',f']]) {
			let node = fragment.getElementById('eq-band1' + prop);
			for (const [i, band] of this.bands.entries()) {
				const addr = `${prefix}/eq/band${i+1}${prop}`;
				node.addEventListener('change', (event) => {
					band[prop] = event.target.value;
					this.drawEQ(svg, grid, curve);
				});
				node = node.nextElementSibling;
			}
		}


		for (const node of fragment.querySelectorAll('*[id]')) {
			if (Channel.#elements.has(node.id)) {
				const type = node.step && node.step < 1 ? ',f' : ',i';
				var prop;
				switch (node.constructor) {
				case HTMLSelectElement:
					prop = 'selectedIndex';
					break;
				case HTMLInputElement:
					switch (node.type) {
					case 'number': prop = 'valueAsNumber'; break;
					case 'checkbox': prop = 'checked'; break;
					}
					break;
				}
				iface.bind(prefix + '/' + node.id.replaceAll('-', '/'), type, node, prop, 'change');
			}
			node.removeAttribute('id');
		}
		this.element = fragment;
	}

	drawEQ(svg, grid, curve) {
		const w = svg.clientWidth;
		const h = svg.clientHeight;
		let d = '';
		for (let i = 0; i < 5; ++i) {
			const y = Math.round((4 + 10 * i) * h / 48) + 0.5;
			d += `M 0 ${y} H ${w} `;
		}
		for (let i = 0; i < 3; ++i) {
			const x = Math.round((7 + 10 * i) * w / 30) + 0.5;
			d += `M ${x} 0 V ${h} `;
		}
		grid.setAttribute('d', d);

		let points = [];
		for (let x = 0; x <= w; ++x) {
			const f2 = Math.pow(10, 2 * ((x - 0.5) * 3 / w + 1.3));
			const f4 = f2 * f2;
			let y = 1;
			for (const band of this.bands)
				y *= (band.a0 + band.a1 * f2 + band.a2 * f4) / (band.b0 + band.b1 * f2 + f4);
			/*
			if (self->lowcut_order) {
				a0 = f2 / (self->lowcut_freq * self->lowcut_freq);
				a = 1;
				for (i = 0; i < self->lowcut_order; ++i) {
					a = a * a0;
				}
				y *= a / (1 + a);
			}
			*/
			y = Math.round(h / 2) + 0.5 + -10 * h / 48 * Math.log10(y);
			points.push(x, y);
		}
		curve.setAttribute('points', points.join(' '));
	}

};

class InputChannel extends Channel {
	static #defaultNames = [
		'Mic/Line 1', 'Mic/Line 2', 'Inst/Line 3', 'Inst/Line 4',
		'Analog 5', 'Analog 6', 'Analog 7', 'Analog 8',
		'SPDIF L', 'SPDIF R', 'AES L', 'AES R',
		'ADAT 1', 'ADAT 2', 'ADAT 3', 'ADAT 4',
		'ADAT 5', 'ADAT 6', 'ADAT 7', 'ADAT 8',
	];

	constructor(iface, index, left) {
		super(Channel.INPUT, iface, InputChannel.#defaultNames[index], `/input/${index + 1}`, left, index);
	}
};

class PlaybackChannel extends Channel {
	static #defaultNames = [
		'Analog 1', 'Analog 2', 'Analog 3', 'Analog 4',
		'Analog 5', 'Analog 6', 'Phones 7', 'Phones 8',
		'SPDIF L', 'SPDIF R', 'AES L', 'AES R',
		'ADAT 1', 'ADAT 2', 'ADAT 3', 'ADAT 4',
		'ADAT 5', 'ADAT 6', 'ADAT 7', 'ADAT 8',
	];

	constructor(iface, index, left) {
		super(Channel.PLAYBACK, iface, PlaybackChannel.#defaultNames[index], `/playback/${index + 1}`, left, index);
	}
};

class OutputChannel extends Channel {
	static #defaultNames = [
		'Analog 1', 'Analog 2', 'Analog 3', 'Analog 4',
		'Analog 5', 'Analog 6', 'Phones 7', 'Phones 8',
		'SPDIF L', 'SPDIF R', 'AES L', 'AES R',
		'ADAT 1', 'ADAT 2', 'ADAT 3', 'ADAT 4',
		'ADAT 5', 'ADAT 6', 'ADAT 7', 'ADAT 8',
	];

	constructor(iface, index, left) {
		super(Channel.OUTPUT, iface, OutputChannel.#defaultNames[index], `/output/${index + 1}`, left, index);
	}
};


class WASI {
	constructor() {
		this.wasi_snapshot_preview1 = new Proxy(this, this);
	}
	static EBADF = 8;
	get(target, prop, receiver) {
		return this[prop].bind(this);
	}
	fd_close(args) {
		console.log(args);
	}
	fd_fdstat_get(args) {
		console.log(args);
	}
	fd_read(fd, iovs, iovsLen, ret) {
		//if (fd != 6)
			return WASI.EBADF;
		iovs = new Uint32Array(this.memory.buffer, iovs, 2 * iovsLen);
		var len = 0;
		for (i = 0; i < iovs.length; ++i) {
		}
		console.log('fd_read', fd, iovs);
		console.log(this.memory);
		console.log(...arguments);
		return -1;
	}
	fd_seek(args) {
		console.log(args);
		return -1;
	}
	fd_write(fd, iovs, iovsLen, ret) {
		iovs = new Uint32Array(this.memory.buffer, iovs, 2 * iovsLen);
		var stderr = ''
		var length = 0;
		for (var i = 0; i < iovs.length; i += 2) {
			length += iovs[i + 1];
			const iov = new Uint8Array(this.memory.buffer, iovs[i], iovs[i + 1]);
			switch (fd) {
			case 1:
				console.log(iov);
				iface.handleOSC(this.memory.buffer, iovs[i], iovs[i + 1]);
				break;
			case 2:
				stderr += new TextDecoder().decode(iov);
				break;
			case 7:
				//console.log(iov);
				this.midiOut.send(iov);
				break;
			}
		}
		switch (fd) {
		case 2:
			console.log(stderr);
			break;
		}
		new Uint32Array(this.memory.buffer, ret)[0] = length;
		//console.log('fd_write', fd, iovs);
		return 0;
	}
	proc_exit(args) {
		console.log(this, args);
	}
}
const iface = new Interface();

const wasi = new WASI()
//const accessPromise = navigator.requestMIDIAccess({sysex: true});
WebAssembly.instantiateStreaming(fetch('oscmix.wasm'), wasi).then(async (obj) => {
	console.log(obj);
	wasi.memory = obj.instance.exports.memory;
	//const access = await accessPromise;
	//const inputs = Array.from(access.inputs.values());
	//const input = inputs[1];
	//const output = Array.from(access.outputs.values())[1];
	//console.log('input', input);
	//wasi.midiOut = output;
	obj.instance.exports._initialize();
	console.log(obj.instance.exports);
	/*
	setInterval(() => {
		obj.instance.exports.timer(0);
	}, 500);
	input.onmidimessage = (message) => {
		if (message.data[0] == 0xf0) {
			if (message.data.length > 1024) {
				console.log('dropping long sysex');
				return;
			}
			const sysex = new Uint8Array(obj.instance.exports.memory.buffer, obj.instance.exports.sysexbuf, message.data.length);
			sysex.set(message.data);
			obj.instance.exports.wasmsysex(message.data.length);
			//console.log(message);
		}
	};
	*/

	//obj.instance.exports.midiread();
});

var midiAccess
function midiStateChange(event) {
	const select = document.getElementById('connection-midi-' + event.port.type);
	switch (event.port.state) {
	case 'connected':
		select.add(new Option(event.port.name, event.port.id));
		break;
	case 'disconnected':
		var i = 0;
		for (const option of select.options) {
			if (option.value == event.port.id) {
				select.remove(i);
				break;
			}
			++i;
		}
		break;
	}
}

function setupMIDI() {
	console.log(midiAccess);
	midiAccess.onstatechange = midiStateChange;
	const midiInput = document.getElementById('connection-midi-input');
	for (const input of midiAccess.inputs.values())
		midiInput.add(new Option(input.name, input.id));
	const midiOutput = document.getElementById('connection-midi-output');
	for (const output of midiAccess.outputs.values())
		midiOutput.add(new Option(output.name, output.id));
}

function setupInterface() {
	const connectionType = document.getElementById('connection-type');
	connectionType.dataset.value = connectionType.value;
	connectionType.addEventListener('change', async (event) => {
		event.target.dataset.value = event.target.value
		switch (event.target.value) {
		case 'MIDI':
			if (!midiAccess) {
				midiAccess = await navigator.requestMIDIAccess({sysex: true});
				setupMIDI()
			}
			break;
		}
	});

	const connectionForm = document.getElementById('connection');
	connectionForm.addEventListener('submit', (event) => {
		event.preventDefault();
		delete document.getElementById('connection-icon').dataset.state;
		switch (event.target.elements['connection-type'].value) {
		case 'WebSocket':
			iface.socket = new WebSocket(event.target.elements['connection-websocket-address'].value);
			break;
		case 'MIDI':
			connectMIDI(event.target);
			break;
		}
	});

	/* make channels */
	const inputs = []
	const playbacks = []
	const outputs = []
	const inputsDiv = document.getElementById('inputs');
	const playbacksDiv = document.getElementById('playbacks');
	const outputsDiv = document.getElementById('outputs');
	for (const [type, array, div] of [[InputChannel, inputs, inputsDiv], [PlaybackChannel, playbacks, playbacksDiv], [OutputChannel, outputs, outputsDiv]]) {
		for (i = 0; i < 20; ++i) {
			const channel = new type(iface, array.length, i % 2 == 1 ? array[i - 1] : null);
			array.push(channel);
			div.appendChild(channel.element);
		}
	}

	iface.bind('/reverb', ',i', document.getElementById('reverb-enabled'), 'checked', 'change');
	const reverbType = document.getElementById('reverb-type');
	const reverbRoomScale = document.getElementById('reverb-roomscale');
	const reverbAttack = document.getElementById('reverb-attack')
	const reverbHold = document.getElementById('reverb-hold')
	const reverbRelease = document.getElementById('reverb-release')
	const reverbTime = document.getElementById('reverb-time')
	const reverbHighDamp = document.getElementById('reverb-highdamp')
	iface.bind('/reverb/type', ',i', reverbType, 'selectedIndex', 'change');
	reverbType.addEventListener('change', (event) => {
		const type = reverbType.selectedIndex;
		console.log(event);
		reverbRoomScale.disabled = type >= 12;
		reverbAttack.disabled = type != 12;
		reverbHold.disabled = type != 12 && type != 13;
		reverbRelease.disabled = type != 12 && type != 13;
		reverbTime.disabled = type != 14;
		reverbHighDamp.disabled = type != 14;
	});
	iface.bind('/reverb/predelay', ',i', document.getElementById('reverb-predelay'), 'valueAsNumber', 'change');
	iface.bind('/reverb/lowcut', ',i', document.getElementById('reverb-lowcut'), 'valueAsNumber', 'change');
	iface.bind('/reverb/roomscale', ',f', reverbRoomScale, 'valueAsNumber', 'change');
	iface.bind('/reverb/attack', ',i', reverbAttack, 'valueAsNumber', 'change');
	iface.bind('/reverb/hold', ',i', reverbHold, 'valueAsNumber', 'change');
	iface.bind('/reverb/release', ',i', reverbRelease, 'valueAsNumber', 'change');
	iface.bind('/reverb/highcut', ',i', document.getElementById('reverb-highcut'), 'valueAsNumber', 'change');
	iface.bind('/reverb/time', ',f', reverbTime, 'valueAsNumber', 'change');
	iface.bind('/reverb/highdamp', ',i', reverbHighDamp, 'valueAsNumber', 'change');
	iface.bind('/reverb/smooth', ',i', document.getElementById('reverb-smooth'), 'valueAsNumber', 'change');
	iface.bind('/reverb/volume', ',f', document.getElementById('reverb-volume'), 'valueAsNumber', 'change');
	iface.bind('/reverb/width', ',f', document.getElementById('reverb-width'), 'valueAsNumber', 'change');
	iface.bind('/echo', ',i', document.getElementById('echo-enabled'), 'checked', 'change');
	iface.bind('/echo/type', ',i', document.getElementById('echo-type'), 'selectedIndex', 'change');
	iface.bind('/echo/delay', ',f', document.getElementById('echo-delay'), 'valueAsNumber', 'change');
	iface.bind('/echo/feedback', ',i', document.getElementById('echo-feedback'), 'valueAsNumber', 'change');
	iface.bind('/echo/hicut', ',i', document.getElementById('echo-highcut'), 'selectedIndex', 'change');
	iface.bind('/echo/volume', ',f', document.getElementById('echo-volume'), 'valueAsNumber', 'change');
	iface.bind('/echo/width', ',f', document.getElementById('echo-width'), 'valueAsNumber', 'change');
	iface.bind('/controlroom/mainout', ',i', document.getElementById('controlroom-mainout'), 'selectedIndex', 'change');
	iface.bind('/controlroom/mainmono', ',i', document.getElementById('controlroom-mainmono'), 'checked', 'change');
	iface.bind('/controlroom/muteenable', ',i', document.getElementById('controlroom-muteenable'), 'checked', 'change');
	iface.bind('/controlroom/dimreduction', ',f', document.getElementById('controlroom-dimreduction'), 'valueAsNumber', 'change');
	iface.bind('/controlroom/dim', ',i', document.getElementById('controlroom-dim'), 'checked', 'change');
	iface.bind('/controlroom/recallvolume', ',f', document.getElementById('controlroom-recallvolume'), 'valueAsNumber', 'change');
	iface.bind('/clock/source', ',i', document.getElementById('clock-source'), 'selectedIndex', 'change');
	iface.bind('/clock/samplerate', ',i', document.getElementById('clock-samplerate'), 'textContent');
	iface.bind('/clock/wckout', ',i', document.getElementById('clock-wckout'), 'checked', 'change');
	iface.bind('/clock/wcksingle', ',i', document.getElementById('clock-wcksingle'), 'checked', 'change');
	iface.bind('/clock/wckterm', ',i', document.getElementById('clock-wckterm'), 'checked', 'change');
	iface.bind('/hardware/opticalout', ',i', document.getElementById('hardware-opticalout'), 'selectedIndex', 'change');
	iface.bind('/hardware/spdifout', ',i', document.getElementById('hardware-spdifout'), 'selectedIndex', 'change');
	iface.bind('/hardware/ccmix', ',i', document.getElementById('hardware-ccmix'), 'selectedIndex', 'change');
	iface.bind('/hardware/standalonemidi', ',i', document.getElementById('hardware-standalonemidi'), 'checked', 'change');
	iface.bind('/hardware/standalonearc', ',i', document.getElementById('hardware-standalonearc'), 'selectedIndex', 'change');
	iface.bind('/hardware/lockkeys', ',i', document.getElementById('hardware-lockkeys'), 'selectedIndex', 'change');
	iface.bind('/hardware/remapkeys', ',i', document.getElementById('hardware-remapkeys'), 'checked', 'change');
}

async function main() {
	document.addEventListener('DOMContentLoaded', setupInterface);
}

main();
