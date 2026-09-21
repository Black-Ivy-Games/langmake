"""Portable application metadata and deterministic icon export; Python stdlib only.

Input artwork: square, opaque, non-interlaced 8-bit RGB/RGBA PNG, 1024..4096
pixels for iOS and 192..4096 for Android. Original artwork stays in the project.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import zlib

PNG = b'\x89PNG\r\n\x1a\n'


def engine_version():
    return json.loads((Path(__file__).resolve().parents[1] / 'dependencies.lock.json').read_text(encoding='utf-8-sig'))['release']


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def metadata(project, target, configuration='debug', overrides=None):
    application = project.get('application')
    if application is not None and not isinstance(application, dict):
        raise ValueError('application must be an object.')
    application = application or {}
    platforms = application.get('platforms', {})
    if not isinstance(platforms, dict) or not isinstance(platforms.get(target, {}), dict):
        raise ValueError('application.platforms must contain platform objects.')
    values = {key: value for key, value in application.items() if key != 'platforms'}
    values.update(platforms.get(target, {}))
    values.update({key: value for key, value in (overrides or {}).items() if value is not None})
    legacy = 'application' not in project
    values.setdefault('label', project.get('title', 'Langmake'))
    release = engine_version()
    release_parts = [int(item) for item in release.split('.')]
    values.setdefault('version', release if legacy else '1.0')
    values.setdefault('build', sum(value * factor for value, factor in zip(release_parts, [10000, 100, 1])) if legacy else 1)
    values.setdefault('orientation', 'landscape')
    identifier = values.get('id')
    pattern = r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+' if target == 'android' else r'[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z0-9][A-Za-z0-9-]*)+'
    if not isinstance(identifier, str) or len(identifier) > 255 or not re.fullmatch(pattern, identifier):
        raise ValueError('Declare application.id (or its %s override) or provide an explicit application/bundle ID.' % target)
    if not isinstance(values['label'], str) or not 1 <= len(values['label']) <= 128 or any(ord(c) < 32 for c in values['label']):
        raise ValueError('Application label must contain 1..128 printable characters.')
    if not isinstance(values['version'], str) or not re.fullmatch(r'[0-9]{1,4}(?:\.[0-9]{1,4}){0,2}', values['version']):
        raise ValueError('Application version must contain one to three numeric components, e.g. 1.2.0.')
    if isinstance(values['build'], bool) or not isinstance(values['build'], int) or not 1 <= values['build'] <= 2100000000:
        raise ValueError('Application build must be an integer in 1..2100000000, independent of save version.')
    if values['orientation'] not in ('landscape', 'portrait', 'sensor'):
        raise ValueError('Application orientation must be landscape, portrait or sensor.')
    warnings = []
    if not values.get('icon') and not legacy:
        if configuration == 'release':
            raise ValueError('Release application metadata requires an icon: declare application.icon or a platform override.')
        warnings.append('Application icon is missing; supply an opaque square PNG before release.')
    return dict(values, metadata_opt_in=not legacy, warnings=warnings)


def icon_path(root, relative):
    root = Path(root).resolve()
    if not isinstance(relative, str) or not relative or '\\' in relative or ':' in relative or any(ord(c) < 32 for c in relative):
        raise ValueError('Application icon must be a project-relative PNG path.')
    name = PurePosixPath(relative)
    if name.is_absolute() or any(part in ('', '.', '..') for part in relative.split('/')):
        raise ValueError('Application icon path must remain inside its project.')
    path = root
    for part in name.parts:
        path /= part
        if path.is_symlink():
            raise ValueError('Application icon must not traverse symlinks.')
    path.resolve().relative_to(root)
    if not path.is_file() or path.suffix.lower() != '.png':
        raise ValueError('Application icon PNG does not exist: ' + relative)
    return path


def decode_png(path, minimum=192):
    path = Path(path)
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('Icon PNG exceeds 64 MiB.')
    data = path.read_bytes()
    if not data.startswith(PNG):
        raise ValueError('Icon must be a PNG image.')
    offset, packed, header, ended = 8, bytearray(), None, False
    palette, transparency, image_started = None, None, False
    while offset < len(data):
        if len(data) - offset < 12:
            raise ValueError('Truncated icon PNG chunk.')
        length, kind = struct.unpack_from('>I4s', data, offset)
        if length > len(data) - offset - 12:
            raise ValueError('Truncated icon PNG data.')
        payload = data[offset + 8:offset + 8 + length]
        expected = struct.unpack_from('>I', data, offset + 8 + length)[0]
        if zlib.crc32(kind + payload) & 0xffffffff != expected:
            raise ValueError('Icon PNG checksum failed.')
        offset += length + 12
        if kind == b'IHDR':
            if header is not None or length != 13 or offset != 33:
                raise ValueError('Invalid icon PNG header.')
            header = struct.unpack('>IIBBBBB', payload)
        elif kind == b'IDAT':
            image_started = True
            packed.extend(payload)
        elif kind == b'PLTE':
            if palette is not None or image_started or not length or length % 3 or length > 768:
                raise ValueError('Invalid icon PNG palette.')
            palette = payload
        elif kind == b'tRNS':
            if transparency is not None or image_started or not header or header[3] != 3 or palette is None or not 1 <= length <= len(palette) // 3:
                raise ValueError('Invalid icon PNG palette transparency; RGB transparency keys are unsupported.')
            transparency = payload
        elif kind == b'IEND':
            if length:
                raise ValueError('Invalid icon PNG end chunk.')
            ended = True
            break
        elif kind == b'acTL':
            raise ValueError('Animated icon PNG is unsupported.')
        elif kind[:1].isupper():
            raise ValueError('Unsupported critical icon PNG chunk.')
    if not ended or offset != len(data) or header is None:
        raise ValueError('Incomplete icon PNG.')
    width, height, depth, color, compression, filtering, interlace = header
    if width != height or not minimum <= width <= 4096:
        raise ValueError('Icon must be square, %s..4096 pixels.' % minimum)
    indexed = color == 3 and depth in (1, 2, 4, 8)
    if not (indexed or (depth == 8 and color in (2, 6))) or compression or filtering or interlace:
        raise ValueError('Icon requires non-interlaced 8-bit RGB/RGBA or 1/2/4/8-bit indexed PNG.')
    if indexed and (palette is None or len(palette) // 3 > (1 << depth)):
        raise ValueError('Icon indexed PNG requires a valid bounded palette.')
    channels = 1 if indexed else 3 if color == 2 else 4
    stride = (width * depth + 7) // 8 if indexed else width * channels
    expected = height * (stride + 1)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(bytes(packed), expected + 1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError('Icon PNG decompressed size is invalid.')
    rgb = bytearray(width * height * 3)
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        mode = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if mode > 4:
            raise ValueError('Unsupported icon PNG row filter.')
        if mode:
            for x in range(stride):
                a = row[x - channels] if x >= channels else 0
                b = previous[x]
                c = previous[x - channels] if x >= channels else 0
                if mode == 1: predictor = a
                elif mode == 2: predictor = b
                elif mode == 3: predictor = (a + b) // 2
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                    predictor = a if pa <= pb and pa <= pc else b if pb <= pc else c
                row[x] = (row[x] + predictor) & 255
        if indexed:
            mask = (1 << depth) - 1
            palette_count = len(palette) // 3
            for x in range(width):
                index = (row[x * depth // 8] >> (8 - depth - x * depth % 8)) & mask
                if index >= palette_count:
                    raise ValueError('Icon PNG pixel refers to a missing palette entry.')
                if transparency is not None and index < len(transparency) and transparency[index] != 255:
                    raise ValueError('Application icons must be opaque; flatten used palette alpha before exporting.')
                base = (y * width + x) * 3
                rgb[base:base + 3] = palette[index * 3:index * 3 + 3]
        elif channels == 4:
            if any(value != 255 for value in row[3::4]):
                raise ValueError('Application icons must be opaque; flatten alpha before exporting.')
            base = y * width * 3
            rgb[base:base + width * 3:3] = row[0::4]
            rgb[base + 1:base + width * 3:3] = row[1::4]
            rgb[base + 2:base + width * 3:3] = row[2::4]
        else:
            rgb[y * width * 3:(y+1) * width * 3] = row
        previous = row
    return width, bytes(rgb)


def png_bytes(size, rgb):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    raw = b''.join(b'\0' + rgb[y * size * 3:(y+1) * size * 3] for y in range(size))
    return PNG + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')


def resize(size, rgb, output_size):
    if size == output_size:
        return rgb
    # Area averaging preserves small details when reducing original artwork.
    result = bytearray(output_size * output_size * 3)
    for y in range(output_size):
        y0, y1 = y * size // output_size, max(y * size // output_size + 1, (y+1) * size // output_size)
        for x in range(output_size):
            x0, x1 = x * size // output_size, max(x * size // output_size + 1, (x+1) * size // output_size)
            sums = [0, 0, 0]
            for sy in range(y0, y1):
                for sx in range(x0, x1):
                    offset = (sy * size + sx) * 3
                    for channel in range(3): sums[channel] += rgb[offset + channel]
            count = (y1-y0) * (x1-x0)
            offset = (y * output_size + x) * 3
            result[offset:offset+3] = bytes(value // count for value in sums)
    return bytes(result)


def generate_icon(source, output, target):
    output = Path(output)
    size, rgb = decode_png(source, 1024 if target == 'ios' else 192)
    files = []
    def emit(name, data):
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        files.append({'path': name, 'bytes': len(data), 'sha256': sha256(path)})
    def document(name, value):
        emit(name, (json.dumps(value, indent=2) + '\n').encode('utf-8'))
    if target == 'ios':
        document('Assets.xcassets/Contents.json', {'info': {'author': 'langmake', 'version': 1}})
        emit('Assets.xcassets/AppIcon.appiconset/icon-1024.png', png_bytes(1024, resize(size, rgb, 1024)))
        document('Assets.xcassets/AppIcon.appiconset/Contents.json', {'images': [
            {'filename': 'icon-1024.png', 'idiom': 'universal', 'platform': 'ios', 'size': '1024x1024'}],
            'info': {'author': 'langmake', 'version': 1}})
    elif target == 'android':
        for density, pixels in [('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)]:
            emit('mipmap-' + density + '/ic_launcher.png', png_bytes(pixels, resize(size, rgb, pixels)))
        # Opaque artwork forms the adaptive background and is masked by Android;
        # use the central 66/108 region for important marks. No extra artwork.
        emit('mipmap-xxxhdpi/ic_launcher_background.png', png_bytes(432, resize(size, rgb, 432)))
        emit('mipmap-anydpi-v26/ic_launcher.xml', b'<?xml version="1.0" encoding="utf-8"?>\n<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android"><background android:drawable="@mipmap/ic_launcher_background"/><foreground android:drawable="@android:color/transparent"/></adaptive-icon>\n')
    else:
        raise ValueError('Unsupported icon target: ' + target)
    return {'source_sha256': sha256(source), 'source_pixels': size, 'target': target, 'files': files,
            'resource_sha256': hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()}
