const textDecoder = new TextDecoder()
const textEncoder = new TextEncoder()

class OSCDecoder {
	constructor(buffer, offset = 0, length = buffer.byteLength) {
		this.buffer = buffer;
		this.offset = offset;
		this.length = length;
	}
	getString() {
		const data = new Uint8Array(this.buffer, this.offset, this.length)
		const end = data.indexOf(0);
		if (end == -1)
			throw new Error('OSC string is not nul-terminated');
		const str = textDecoder.decode(data.subarray(0, end));
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

class Interface {
	constructor(socket) {
		this.socket = socket;
		this.methods = new Map();
		socket.onmessage = async (event) => {
			this.#decodeMessage(await event.data.arrayBuffer());
		};
	}

	/*
	set socket(newSocket) {
		if (this.#socket) {
			this.#socket.removeEventListener
		this.#socket = newSocket;
		
	}
	*/

	#decodeMessage(buffer, offset, length) {
		const decoder = new OSCDecoder(buffer, offset, length);
		const addr = decoder.getString();
		if (addr == '#bundle') {
			decoder.getInt();
			decoder.getInt();
			while (decoder.length > 0) {
				const length = decoder.getInt();
				if (length % 4 != 0)
					throw new Error('OSC bundle has invalid padding');
				this.#decodeMessage(buffer, decoder.offset, length);
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
			//console.log(addr, args);
			const method = this.methods.get(addr);
			if (method)
				method(args);
		}
	}

	#putString(value) {
		const data = new Uint8Array(this.buffer, this.offset, value.length);
		const { read } = textEncoder.encodeInto(value, data);
		if (read < value.length)
			throw new Error('string contains non-ASCII characters');
		this.offset += (value.length + 4) & -4;
	}

	#putInt(value) {
		this.view.setInt32(this.offset, value);
		this.offset += 4;
	}

	#putFloat(value) {
		this.view.setFloat32(this.offset, value);
		this.offset += 4;
	}

	send(addr, types, args) {
		if (types[0] != ',' || types.length != 1 + args.length)
			throw new Error('invalid type string');
		console.log('send', addr, args);
		let length = ((addr.length + 4) & -4) + ((types.length + 4) & -4);
		for (const [i, arg] of args.entries()) {
			switch (types[1 + i]) {
			case 'i': length += 4; break;
			case 'f': length += 4; break;
			case 's': length += (arg.length + 4) & -4; break;
			}
		}
		this.buffer = new ArrayBuffer(length);
		this.view = new DataView(this.buffer);
		this.offset = 0;
		this.#putString(addr);
		this.#putString(types);
		for (const [i, arg] of args.entries()) {
			switch (types[1 + i]) {
			case 'i': this.#putInt(arg); break;
			case 'f': this.#putFloat(arg); break;
			case 's': this.#putString(arg); break;
			}
		}
		console.log('sending', this.buffer);
		this.socket.send(this.buffer);
	}

	bind(addr, types, obj, prop, eventType) {
		//console.log(addr, types, obj, eventType, prop);
		this.methods.set(addr, (args) => {
			obj[prop] = args[0]
			obj.dispatchEvent(new DeviceEvent('change', {skip: true}))
		});
		if (eventType) {
			obj.addEventListener(eventType, (event) => {
				if (!(event instanceof DeviceEvent))
					this.send(addr, types, [obj[prop]]);
			});
		}
	}
};

class DeviceEvent extends Event {}

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
	constructor(client, name, prefix, left) {
		this.client = client;
		this.prefix = prefix;

		const template = document.getElementById('channel-template');
		const fragment = template.content.cloneNode(true);

		this.volumeDiv = fragment.getElementById('channel-volume');

		const nameDiv = fragment.getElementById('channel-name');
		nameDiv.textContent = name;

		this.level = fragment.getElementById('channel-level');
		client.methods.set(prefix + '/level', (args) => {
			const value = Math.max(args[0], -65);
			if (this.level.value != value)
				this.level.value = value;
		});

		const stereo = fragment.getElementById('channel-stereo');
		this.stereo = stereo;
		if (left) {
			stereo.addEventListener('change', (event) => {
				if (stereo.checked) {
					left.volumeDiv.insertBefore(this.level, left.level.nextSibling);
				} else {
					this.volumeDiv.insertBefore(this.level, this.volumeDiv.firstElementChild);
				}
			});
		}
		client.bind(prefix + '/stereo', ',i', stereo, 'checked', 'change');
		client.bind(prefix + '/mute', ',i', fragment.getElementById('channel-mute'), 'checked', 'change');
		client.bind(prefix + '/hi-z', ',i', fragment.getElementById('channel-hi-z'), 'checked', 'change');
		client.bind(prefix + '/reflevel', ',i', fragment.getElementById('channel-reflevel'), 'selectedIndex', 'change');
		client.bind(prefix + '/gain', ',i', fragment.getElementById('channel-gain'), 'value', 'change');

		/*
		const stereo = fragment.getElementById('channel-stereo');
		stereo.addEventListener('change', (event) => {
			console.log('change', event);
			this.client.send(prefix + '/stereo', ',i', [stereo.checked]);
		});
		client.methods.set(prefix + '/stereo', (args) => {
			stereo.checked = args[0];
		});
		this.stereo = stereo;
		*/

		const volumeRange = fragment.getElementById('channel-volume-range');
		const volumeNumber = fragment.getElementById('channel-volume-number');
		volumeRange.oninput = volumeNumber.oninput = (event) => {
			this.client.send(this.prefix + '/volume', ',f', [event.target.value]);
			volumeRange.value = event.target.value;
			volumeNumber.value = event.target.value;
		};
		client.methods.set(prefix + '/volume', (args) => {
			volumeRange.value = args[0];
			volumeNumber.value = args[0];
		});

		const onPanelButtonChanged = (event) => {
			for (const label of event.target.parentNode.parentNode.children) {
				const other = label.firstElementChild;
				if (other != event.target)
					other.checked = false;
			}
		};
		for (const node of fragment.querySelectorAll('.panel-buttons input[type="checkbox"]'))
			node.onchange = onPanelButtonChanged;

		const svg = fragment.getElementById('channel-eq-plot');
		const grid = fragment.getElementById('channel-eq-grid');
		const curve = fragment.getElementById('channel-eq-curve');
		this.bands = [new EQBand(), new EQBand(), new EQBand()];
		const observer = new ResizeObserver(() => {this.drawEQ(svg, grid, curve)});
		observer.observe(svg);

		const band1Type = fragment.getElementById('channel-eq-band1-type')
		client.bind(prefix + '/eq/band1type', ',i', band1Type, 'selectedIndex', 'change');
		band1Type.addEventListener('change', (event) => {
			this.bands[0].type = EQBand[event.target.value];
			this.drawEQ(svg, grid, curve);
		});
		const band3Type = fragment.getElementById('channel-eq-band3-type')
		client.bind(prefix + '/eq/band3type', ',i', band3Type, 'selectedIndex', 'change');
		band3Type.addEventListener('change', (event) => {
			this.bands[2].type = EQBand[event.target.value];
			this.drawEQ(svg, grid, curve);
		});

		for (const [prop, type] of [['gain', ',f'], ['freq', ',i'], ['q', ',f']]) {
			let node = fragment.getElementById('channel-eq-band1-' + prop);
			for (const [i, band] of this.bands.entries()) {
				const addr = `${prefix}/eq/band${i+1}${prop}`;
				client.bind(addr, type, node, 'value', 'change');
				node.addEventListener('change', (event) => {
					band[prop] = event.target.value;
					this.drawEQ(svg, grid, curve);
				});
				node = node.nextElementSibling;
			}
		}

		for (const node of fragment.querySelectorAll('*[id]'))
			node.removeAttribute('id');
		if (!left) {
			fragment.children[0].classList.add('channel-left')
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
			//console.log(grid);
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

	constructor(client, index, left) {
		super(client, InputChannel.#defaultNames[index], `/input/${index + 1}`, left);
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

	constructor(client, index, left) {
		super(client, PlaybackChannel.#defaultNames[index], `/playback/${index + 1}`, left);
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

	constructor(client, index, left) {
		super(client, OutputChannel.#defaultNames[index], `/output/${index + 1}`, left);
	}
};

document.addEventListener('DOMContentLoaded', async () => {
	const socket = new WebSocket('ws://localhost:7222');
	socket.onclose = (event) => console.log(event);
	const test = await new Promise((accept, reject) => {
		socket.onopen = (event) => accept(event);
		socket.onerror = (event) => reject(event);
	});
	console.log(test);
	const iface = new Interface(socket);
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

	const reverbPredelay = document.getElementById('reverb-predelay');
	reverbPredelay.addEventListener('wheel', (event) => {
		event.target.valueAsNumber += 1;
		console.log(event);
		event.preventDefault();
	}, {passive: false});
	reverbPredelay.addEventListener('pointerup', (event) => {
		reverbPredelay.onpointermove = null;
		reverbPredelay.releasePointerCapture(event.pointerId);
	});
	reverbPredelay.addEventListener('pointerdown', (event) => {
		const y0 = event.clientY;
		const v0 = reverbPredelay.valueAsNumber;
		const min = reverbPredelay.min;
		const max = reverbPredelay.max;
		const step = reverbPredelay.step;
		reverbPredelay.onpointermove = (event) => {
			console.log(y0 - event.clientY);
			reverbPredelay.valueAsNumber = Math.round(Math.max(Math.min((y0 - event.clientY) * (reverbPredelay.max - reverbPredelay.min) / 200, reverbPredelay.max), reverbPredelay.min) / step) * step;
		};
		reverbPredelay.setPointerCapture(event.pointerId);
		console.log(event);
	});

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
});
