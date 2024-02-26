'use strict';

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
}

const WASI = {
	EBADF: 8,
	ENOTSUP: 58,
};

class ConnectionWebSocket extends AbortController {
	constructor(socket) {
		super();
		this.ready = new Promise((resolve, reject) => {
			socket.addEventListener('open', resolve, {once: true, signal: this.signal});
			socket.addEventListener('close', (event) => {
				const error = new Error('WebSocket closed with code ' + event.code);
				reject(error);
				this.abort(error);
			}, {once: true, signal: this.signal});
			this.signal.addEventListener('abort', (event) => {
				reject(event.target.reason);
				socket.close();
			}, {once: true});
		});
		socket.addEventListener('message', (event) => {
			if (this.recv)
				event.data.arrayBuffer().then(this.recv.bind(this));
		}, {signal: this.signal});
		this.send = (data) => {
			socket.send(data);
		}
	}
}

class ConnectionMIDI extends AbortController {
	static #module;
	constructor(input, output) {
		super();
		let instance;
		const imports = {
			env: {
				writeosc: function(buf, len) {
					if (this.recv)
						this.recv(instance.exports.memory.buffer, buf, len);
				}.bind(this),
				writemidi(buf, len) {
					output.send(new Uint8Array(instance.exports.memory.buffer, buf, len));
				},
			},
			wasi_snapshot_preview1: {
				fd_close() { return WASI.ENOTSUP; },
				fd_fdstat_get() { return WASI.ENOTSUP; },
				fd_seek() { return WASI.ENOTSUP; },
				fd_write(fd, iovsPtr, iovsLen, ret) {
					if (fd != 2)
						return WASI.EBADF;
					const text = new TextDecoder();
					const memory = instance.exports.memory.buffer;
					const iovs = new Uint32Array(memory, iovsPtr, 2 * iovsLen);
					let stderr = ''
					let length = 0;
					for (let i = 0; i < iovs.length; i += 2) {
						length += iovs[i + 1];
						const iov = new Uint8Array(memory, iovs[i], iovs[i + 1]);
						stderr += text.decode(iov);
					}
					console.log(stderr);
					new Uint32Array(memory, ret)[0] = length;
					return 0;
				},
				proc_exit: function(status) {
					this.abort(new Error('oscmix.wasm exited with status ' + status));
				}.bind(this),
			},
		};
		if (!ConnectionMIDI.#module)
			ConnectionMIDI.#module = WebAssembly.compileStreaming(fetch('oscmix.wasm'));
		this.ready = ConnectionMIDI.#module.then(async (module) => {
			instance = await WebAssembly.instantiate(module, imports);
			this.signal.throwIfAborted();
			for (const symbol of ['jsdata', 'jsdatalen']) {
				if (!(symbol in instance.exports))
					throw Error(`wasm module does not export '${symbol}'`);
			}
			const jsdata = instance.exports.jsdata;
			const jsdataLen = new Uint32Array(instance.exports.memory.buffer, instance.exports.jsdatalen, 4)[0];

			instance.exports._initialize();
			const name = new Uint8Array(instance.exports.memory.buffer, jsdata, jsdataLen);
			const { read } = new TextEncoder().encodeInto(input.name + '\0', name);
			if (read < input.name.length + 1)
				throw Error('MIDI port name is too long');
			if (instance.exports.init(jsdata) != 0)
				throw Error('oscmix init failed');
			input.addEventListener('midimessage', (event) => {
				if (event.data[0] != 0xf0 || event.data[event.data.length - 1] != 0xf7)
					return;
				if (event.data.length > jsdataLen) {
					console.warn('dropping long sysex');
					return;
				}
				const sysex = new Uint8Array(instance.exports.memory.buffer, jsdata, event.data.length);
				sysex.set(event.data);
				instance.exports.handlesysex(sysex.byteOffset, sysex.byteLength, jsdata);
			}, {signal: this.signal});
			const stateHandler = (event) => {
				if (event.target.state == 'disconnected')
					this.abort();
			};
			input.addEventListener('statechange', stateHandler, {signal: this.signal});
			output.addEventListener('statechange', stateHandler, {signal: this.signal});
			await Promise.all([input.open(), output.open()]);
			this.signal.throwIfAborted();
			const interval = setInterval(instance.exports.handletimer.bind(null, true), 100);
			this.signal.addEventListener('abort', () => {
				clearInterval(interval)
				input.close();
				output.close();
			}, {once: true});
		}).catch((error) => {
			this.abort(error);
			throw error;
		});
		this.send = (data) => {
			const osc = new Uint8Array(instance.exports.memory.buffer, instance.exports.jsdata, data.length);
			osc.set(data);
			instance.exports.handleosc(osc.byteOffset, osc.byteLength);
		};
	}
}

class Interface {
	constructor() {
		this.methods = new Map();
	}

	#connection;
	set connection(conn) {
		this.#connection = conn;
		conn.recv = this.handleOSC.bind(this);
		conn.signal.addEventListener('abort', () => this.#connection = null, {once: true});
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
			if (!addr.match(/\/level$/))
				console.debug(addr, args);
			const method = this.methods.get(addr);
			if (method)
				method(args);
		}
	}

	send(addr, types, args) {
		if (!this.#connection)
			throw new Error('not connected');
		if (types[0] != ',' || types.length != 1 + args.length)
			throw new Error('invalid OSC type string');
		console.debug(addr, types, args);
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
		this.#connection.send(encoder.data());
	}

	bind(addr, types, obj, prop, eventType) {
		this.methods.set(addr, (args) => {
			const step = obj.step;
			obj[prop] = step ? Math.round(args[0] / step) * step : args[0];
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
}

class OSCEvent extends Event {}

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
	eval(f2, f4) {
		return (this.a0 + this.a1 * f2 + this.a2 * f4) / (this.b0 + this.b1 * f2 + f4);
	}
}

class LowCut {
	static #k = [1, 0.655, 0.528, 0.457];
	order = 1;
	freq = 100;
	eval(f2) {
		const freq = this.freq * LowCut.#k[this.order];
		const freq2 = freq * freq;
		let y = 1;
		for (let i = 0; i <= this.order; ++i)
			y *= f2 / (f2 + freq2);
		return y;
	}
}

class EQPlot {
	#svg;
	#grid;
	#curve;
	bands = [];
	constructor(svg) {
		this.#svg = svg;
		this.#curve = svg.querySelector('.eq-curve');;
		const grid = svg.querySelector('.eq-grid');
		const observer = new ResizeObserver(() => {
			const w = svg.clientWidth;
			const h = svg.clientHeight;
			const d = [];
			for (let i = 0; i < 5; ++i)
				d.push(`M 0 ${Math.round((4 + 10 * i) * h / 48) + 0.5} H ${w}`);
			for (let i = 0; i < 3; ++i)
				d.push(`M ${Math.round((7 + 10 * i) * w / 30) + 0.5} 0 V ${h}`);
			grid.setAttribute('d', d.join(' '));
			this.update();
		});
		observer.observe(svg);
	}
	update() {
		const w = this.#svg.clientWidth;
		const h = this.#svg.clientHeight;
		let points = [];
		for (let x = 0; x <= w; ++x) {
			const f2 = Math.pow(10, 2 * ((x - 0.5) * 3 / w + 1.3));
			const f4 = f2 * f2;
			let y = 1;
			for (const band of this.bands) {
				if (band.enabled)
					y *= band.eval(f2, f4);
			}
			y = Math.round(h / 2) + 0.5 + -10 * h / 48 * Math.log10(y);
			points.push(x, y);
		}
		this.#curve.setAttribute('points', points.join(' '));
	}
}

class Channel {
	static INPUT = 0;
	static OUTPUT = 1;
	static PLAYBACK = 2;

	static #inputNames = [
		'Mic/Line 1', 'Mic/Line 2', 'Inst/Line 3', 'Inst/Line 4',
		'Analog 5', 'Analog 6', 'Analog 7', 'Analog 8',
		'SPDIF L', 'SPDIF R', 'AES L', 'AES R',
		'ADAT 1', 'ADAT 2', 'ADAT 3', 'ADAT 4',
		'ADAT 5', 'ADAT 6', 'ADAT 7', 'ADAT 8',
	];
	static #outputNames = [
		'Analog 1', 'Analog 2', 'Analog 3', 'Analog 4',
		'Analog 5', 'Analog 6', 'Phones 7', 'Phones 8',
		'SPDIF L', 'SPDIF R', 'AES L', 'AES R',
		'ADAT 1', 'ADAT 2', 'ADAT 3', 'ADAT 4',
		'ADAT 5', 'ADAT 6', 'ADAT 7', 'ADAT 8',
	];

	static #elements = new Set([
		'mute',
		'fx',
		'stereo',
		'record',
		'playchan',
		'msproc',
		'phase',
		'gain',
		'48v',
		'reflevel',
		'autoset',
		'hi-z',
		'eq',
		'eq/band1type',
		'eq/band1gain',
		'eq/band1freq',
		'eq/band1q',
		'eq/band2gain',
		'eq/band2freq',
		'eq/band2q',
		'eq/band3type',
		'eq/band3gain',
		'eq/band3freq',
		'eq/band3q',
		'lowcut',
		'lowcut/freq',
		'lowcut/slope',
		'dynamics',
		'dynamics/gain',
		'dynamics/attack',
		'dynamics/release',
		'dynamics/compthres',
		'dynamics/compratio',
		'dynamics/expthres',
		'dynamics/expratio',
		'autolevel',
		'autolevel/maxgain',
		'autolevel/headroom',
		'autolevel/risetime',
	]);

	constructor(type, index, iface, left) {
		const template = document.getElementById('channel-template');
		const fragment = template.content.cloneNode(true);

		let name, prefix;
		const flags = new Set();
		switch (type) {
		case Channel.INPUT:
			flags.add('input');
			if (index == 0 || index == 1)
				flags.add('mic');
			if (index == 2 || index == 3)
				flags.add('inst');
			if (index <= 7) {
				flags.add('analog');
				flags.add('analog-input');
			}
			if (index >= 4 && index <= 7)
				flags.add('line');
			name = Channel.#inputNames[index];
			prefix = `/input/${index + 1}`;
			break;
		case Channel.PLAYBACK:
			flags.add('playback');
			name = Channel.#outputNames[index];
			prefix = `/playback/${index + 1}`;
			break;
		case Channel.OUTPUT:
			flags.add('output');
			if (index <= 7)
				flags.add('analog');
			name = Channel.#outputNames[index];
			prefix = `/output/${index + 1}`;
			break;
		}

		for (const node of fragment.querySelectorAll('[data-flags]')) {
			let found
			for (const flag of node.dataset.flags.split(' ')) {
				if (flags.has(flag)) {
					found = true;
					break;
				}
			}
			if (!found)
				node.remove();
		}

		this.volumeDiv = fragment.getElementById('channel-volume');

		const nameDiv = fragment.getElementById('channel-name');
		nameDiv.textContent = name;

		this.level = fragment.getElementById('channel-level');
		iface.methods.set(prefix + '/level', (args) => {
			const value = Math.max(args[0], -65);
			if (this.level.value != value)
				this.level.value = value;
		});

		const stereo = fragment.getElementById('stereo');
		if (left) {
			stereo.addEventListener('change', (event) => {
				if (event.target.checked) {
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

		const volumeRange = fragment.getElementById('volume-range');
		const volumeNumber = fragment.getElementById('volume-number');
		if (type == Channel.OUTPUT) {
			volumeRange.oninput = volumeNumber.onchange = (event) => {
				volumeRange.value = event.target.value;
				volumeNumber.value = event.target.value;
				iface.send(prefix + '/volume', ',f', [event.target.value]);
			};
			iface.methods.set(prefix + '/volume', (args) => {
				volumeRange.value = args[0];
				volumeNumber.value = args[0];
			});
		} else {
			const output = fragment.getElementById('volume-output');
			output.addEventListener('change', (event) => {
				volumeRange.value = volumeNumber.value = this.volume[event.target.selectedIndex];
			});
			volumeRange.oninput = volumeNumber.onchange = (event) => {
				volumeRange.value = volumeNumber.value = event.target.value;
				this.volume[output.selectedIndex] = event.target.value;
				iface.send(`/mix/${output.selectedIndex+1}${prefix}`, ',f', [event.target.value]);
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

		const eqSvg = fragment.getElementById('eq-plot');
		if (eqSvg) {
			this.eq = new EQPlot(eqSvg);

			const eqEnabled = fragment.getElementById('eq');
			eqEnabled.addEventListener('change', (event) => {
				for (let i = 0; i < 3; ++i)
					this.eq.bands[i].enabled = event.target.checked;
				this.eq.update();
			});
			const band1Type = fragment.getElementById('eq/band1type')
			band1Type.addEventListener('change', (event) => {
				this.eq.bands[0].type = EQBand[event.target.value];
				this.eq.update();
			});
			const band3Type = fragment.getElementById('eq/band3type')
			band3Type.addEventListener('change', (event) => {
				this.eq.bands[2].type = EQBand[event.target.value];
				this.eq.update();
			});
			for (let i = 0; i < 3; ++i) {
				const band = new EQBand();
				this.eq.bands.push(band);
				for (const prop of ['gain', 'freq', 'q']) {
					const node = fragment.getElementById(`eq/band${i+1}${prop}`);
					node.addEventListener('change', (event) => {
						band[prop] = event.target.value;
						this.eq.update();
					});
				}
			}

			const lowCut = new LowCut();
			this.eq.bands.push(lowCut);
			fragment.getElementById('lowcut').addEventListener('change', (event) => {
				lowCut.enabled = event.target.checked;
				this.eq.update();
			});
			fragment.getElementById('lowcut/slope').addEventListener('change', (event) => {
				lowCut.order = event.target.selectedIndex;
				this.eq.update();
			});
			fragment.getElementById('lowcut/freq').addEventListener('change', (event) => {
				lowCut.freq = event.target.value;
				this.eq.update();
			});
		}

		for (const node of fragment.querySelectorAll('[id]')) {
			if (Channel.#elements.has(node.id)) {
				const type = node.step && node.step < 1 ? ',f' : ',i';
				let prop;
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
				iface.bind(prefix + '/' + node.id, type, node, prop, 'change');
			}
			node.removeAttribute('id');
		}
		this.element = fragment;
	}
}

const iface = new Interface();

function setupInterface() {
	const connectionType = document.getElementById('connection-type');

	const midiPorts = {
		input: document.getElementById('connection-midi-input'),
		output: document.getElementById('connection-midi-output'),
	};
	for (const select of [midiPorts.input, midiPorts.output])
		select.addEventListener('change', (event) => event.target.dataset.id = event.target.value);
	const midiOption = document.getElementById('connection-type-midi');
	function midiAccessChanged(status) {
		const denied = status.state == 'denied';
		midiOption.disabled = denied;
		if (denied) {
			connectionType.selectedIndex = 0;
			connectionType.dataset.value = connectionType.value
		}
	}
	navigator.permissions.query({name: 'midi', sysex: true}).then((status) => {
		midiAccessChanged(status);
		status.onchange = (event) => midiAccessChanged(event.target);
	});
	function midiStateChanged(event) {
		const select = midiPorts[event.port.type];
		switch (event.port.state) {
		case 'connected':
			select.add(new Option(event.port.name, event.port.id));
			break;
		case 'disconnected':
			let i = 0;
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

	let midiAccess;
	connectionType.dataset.value = connectionType.value;
	connectionType.addEventListener('change', (event) => {
		event.target.dataset.value = event.target.value
		if (midiAccess) {
			midiPorts.input.replaceChildren();
			midiPorts.output.replaceChildren();
			midiPorts.input.disabled = true;
			midiPorts.output.disabled = true;
			midiAccess.removeEventListener('statechange', midiStateChanged);
			midiAccess = null;
		}
		if (event.target.value == 'MIDI') {
			navigator.requestMIDIAccess({sysex: true}).then((access) => {
				if (event.target.value != 'MIDI')
					return;
				for (const [select, ports] of [[midiPorts.input, access.inputs], [midiPorts.output, access.outputs]]) {
					let prev, defaultOption;
					for (const port of ports.values()) {
						const option = new Option(port.name, port.id);
						select.add(option);
						if (port.id == select.dataset.id)
							option.selected = true;
						if (port.name.match(/^Fireface UCX II \(/) && port.name == prev)
							defaultOption = option;
						else
							prev = port.name;
					}
					if (select.value != select.dataset.id && defaultOption)
						defaultOption.selected = true;
					select.dataset.id = select.value;
					select.disabled = false;
				}
				midiAccess = access;
				midiAccess.addEventListener('statechange', midiStateChanged);
			});
		}
	});

	const icon = document.getElementById('connection-icon');

	let connection;
	const connectionForm = document.getElementById('connection');
	connectionForm.addEventListener('submit', (event) => {
		event.preventDefault();
		if (connection)
			connection.abort();
		delete icon.dataset.state;
		if (event.submitter.id == 'connection-disconnect') {
			icon.textContent = '';
			return;
		}
		const elements = event.target.elements;
		icon.textContent = elements['connection-type'].value;
		switch (elements['connection-type'].value) {
		case 'WebSocket':
			connection = new ConnectionWebSocket(new WebSocket(elements['connection-websocket-address'].value));
			break;
		case 'MIDI':
			const input = midiAccess.inputs.get(elements['connection-midi-input'].value);
			if (!input)
				throw new Error('no MIDI input');
			const output = midiAccess.outputs.get(elements['connection-midi-output'].value);
			if (!output)
				throw new Error('no MIDI output');
			connection = new ConnectionMIDI(input, output);
			break;
		default:
			throw new Error('unknown connection type');
		}
		connection.signal.addEventListener('abort', () => {
			icon.dataset.state = 'failed';
			connection = null;
		}, {once: true});
		connection.ready.then(() => {
			iface.connection = connection;
			icon.textContent = elements['connection-type'].value;
			icon.dataset.state = 'connected';
			iface.send('/refresh', ',', []);
		}).catch(console.error);
	});

	/* make channels */
	for (const [type, id] of [[Channel.INPUT, 'inputs'], [Channel.PLAYBACK, 'playbacks'], [Channel.OUTPUT, 'outputs']]) {
		const div = document.getElementById(id);
		let left;
		for (let i = 0; i < 20; ++i) {
			const channel = new Channel(type, i, iface, left);
			div.appendChild(channel.element);
			left = i % 2 == 0 ? channel : null;
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
		const type = event.target.selectedIndex;
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

	/* allow scrolling on number and range inputs */
	const wheel = (event) => {
		event.preventDefault();
		event.target.valueAsNumber = Math.min(Math.max(event.target.valueAsNumber - event.deltaY * Number(event.target.step) / 180, event.target.min), event.target.max);
		event.target.dispatchEvent(new Event(event.target.type == 'range' ? 'input' : 'change'));
	};
	const focus = (event) => event.target.addEventListener('wheel', wheel, {passive: false});
	const blur = (event) => event.target.removeEventListener('wheel', wheel);
	for (const node of document.querySelectorAll('input[type="number"], input[type="range"]')) {
		node.addEventListener('focus', focus);
		node.addEventListener('blur', blur);
	}
}

document.addEventListener('DOMContentLoaded', setupInterface);
