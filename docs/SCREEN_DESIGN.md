# PomoCoro — Screen Design Document

**Device**: M5Stack ATOMS3  
**Display**: 0.85" IPS LCD / 128×128 px  
**Version**: v1.0  

---

## Screen List & Transitions

```
                    ┌─────────────┐
                    │  BOOT       │  Power on (2 sec)
                    └──────┬──────┘
                           │ auto
                    ┌──────┴──────┐
                    │  HOME       │ ◀─── Upright (initial state)
                    └──────┬──────┘
        Tilt right ↓       │ Tilt left ↑
                    ┌──────┴──────┐
                    │  FOCUS      │ ◀─── 90° right
                    └──────┬──────┘
        Tilt right ↓       │ Tilt left ↑
                    ┌──────┴──────┐
                    │  LONG BREAK │ ◀─── 180° further
                    └──────┬──────┘
        Tilt right ↓       │ Tilt left ↑
                    ┌──────┴──────┐
                    │  TODAY      │ ◀─── 180° further
                    └─────────────┘

  Lay flat (face up) → SETTINGS  (from any state)
  Tilt left reverses the order: TODAY → LONG BREAK → FOCUS → HOME
```

---

## 1. BOOT Screen

**Orientation**: Any (shown at power-on)  
**Display rotation**: 0  
**Background**: `#0D1B2A`  

### Layout

```
┌────────────────┐
│                │
│                │
│   POMOCORO     │  y=38  Gradient text (orange → crimson)
│                │        DM Mono bold 22px
│  Connecting... │  y=72  Status text #AABBCC (light grey)
│                │
│                │
└────────────────┘
```

### Text Colors

| Element | Color |
|---------|-------|
| Logo "POMOCORO" | Gradient `#F0A030` → `#E05828` → `#C03060` |
| Status text | `#AABBCC` (light grey) |

### Status Messages

| Phase | Message |
|-------|---------|
| Connecting to WiFi | `{SSID}...` |
| NTP sync | `Syncing NTP...` |
| WiFi failed | `Offline Mode` |

### Behavior
- Shown for approximately 2 seconds at startup
- Automatically transitions to HOME screen after init completes

---

## 2. HOME Screen

**Orientation**: Vertical, display facing user (USB connector at bottom)  
**Display rotation**: 0  
**Background**: `#0D1B2A`  

### Layout

```
┌────────────────┐  y=0
│                │
│    HH:MM       │  y=8   DM Mono bold 28px / #E8F0F8
│                │
│   M/D  DAY     │  y=42  DM Mono 16px / #8899AA
│────────────────│  y=64  Divider line (gradient, fades at edges)
│                │
│  [icon]  XX°C  │  y=76  Weather icon (left) + temp (right)
│                │        Temp: DM Mono bold 16px / #C8D8E8
│                │
└────────────────┘
```

### Display Elements

| Element | Content | Update Interval |
|---------|---------|----------------|
| Time | HH:MM (NTP synced, JST) | Every 10 sec |
| Date | M/D + 3-letter day (SUN/MON/TUE…) | Every 10 sec |
| Weather icon | Drawn with primitives (see Weather Icons) | Every 10 min |
| Temperature | °C, from Open-Meteo API | Every 10 min |

**Offline**: Temperature area shows `Loading...` (`#445566`)

### Button Tap
- **Tap**: Manually refresh weather (WiFi only)

---

## 3. FOCUS Screen

**Orientation**: Tilt 90° to the right  
**Display rotation**: 3  

### 3A. Digital Mode

**Background**: `#120808` → `#0A0506` (gradient)  
**Accent**: `#DD5555` (red)

```
┌────────────────┐
│     FOCUS      │  y=8   DM Mono 9px / dark red
│                │
│    MM:SS       │  y=26  DM Mono bold 26px / #F0DDDD
│                │
│████████░░░░░░░ │  y=54  Progress bar 24px height, pill-shaped
│                │        Fill: #DD5555 / Track: #1A2535
└────────────────┘
```

### 3B. Circle Mode

**Background**: `#120808`

```
┌────────────────┐
│   ╔══════╗    │
│   ║      ║    │  Donut: outer r=58, inner r=34
│   ║ FOCUS║    │  12 o'clock start, clockwise fill
│   ║      ║    │  Track: #491C1C (fill ÷ 3)
│   ╚══════╝    │  Fill:  #DD5555
│       ●        │  12 o'clock marker: white circle r=3
└────────────────┘
```

### Phase Transition

| Phase | Duration | Label | Bar / Donut Color |
|-------|----------|-------|-------------------|
| Focus | 25 min | `FOCUS` | `#DD5555` |
| Short break | 5 min | `BREAK` | `#55EE88` |

Break phase background: `#081208` → `#050A06` (dark green gradient)

### Timer Completion Alerts

| Event | Flash Color | Dark Color | Count | Duration |
|-------|-------------|------------|-------|---------|
| Focus complete | `#44FF88` | `#001A08` | 5× | 180ms on / 100ms off |
| Break complete | `#FF4444` | `#1A0000` | 5× | 180ms on / 100ms off |

### Button Tap
- **Tap (Focus phase)**: Restart 25-min timer
- **Tap (Break phase)**: Restart 5-min timer

---

## 4. LONG BREAK Screen

**Orientation**: Tilt 180° further from FOCUS  
**Display rotation**: 2  
**Background**: `#060E18` → `#040A14` (gradient)  
**Accent**: `#3A80C0` (blue)

### 4A. Digital Mode

```
┌────────────────┐
│   LONG BREAK   │  y=8   DM Mono 9px / #1E4060
│                │
│    MM:SS       │  y=26  DM Mono bold 26px / #8AB8E0
│                │
│████████░░░░░░░ │  y=54  Progress bar 24px / Fill: #3A80C0
│                │
└────────────────┘
```

### 4B. Circle Mode

Same donut layout as FOCUS.  
Fill: `#3A80C0` / Track: `#132A40` (fill ÷ 3)  
Center label: `LONG BREAK` (DM Mono 11px / `#8AB8E0`)

### Timer Completion Alert

| Flash Color | Dark Color | Count | Duration |
|-------------|------------|-------|---------|
| `#44AAFF` | `#00081A` | 5× | 180ms on / 100ms off |

### Timer Duration
Configurable: **15 min** or **20 min** (set in SETTINGS screen)

### Button Tap
- **Tap**: End break early → switch to FOCUS (25 min restart)

---

## 5. TODAY Screen

**Orientation**: Tilt 180° further from LONG BREAK  
**Display rotation**: 1  
**Background**: `#1E1040`  

### Layout

```
┌────────────────┐
│     TODAY      │  y=6   DM Mono 16px / #6644AA
│────────────────│  y=30  Divider (gradient, fades at edges)
│                │
│       3        │  y=36  Session count  DM Mono bold 36px / #FFFFFF
│    sessions    │  y=68  DM Mono 16px / #6644AA
│────────────────│  y=90  Divider
│    75 min      │  y=96  Focus time  DM Mono bold 18px / #FFFFFF
│                │        (format: Xh Ym when ≥ 60 min)
└────────────────┘
```

### Data

| Element | Content | Reset |
|---------|---------|-------|
| Session count | Completed 25-min focus sessions (today) | Daily (midnight) |
| Focus time | Total focused minutes (today) | Daily (midnight) |

**Persistence**: Stored in NVS (non-volatile). Survives power-off.

### Button Tap
- **Tap**: Clear today's records immediately (NVS updated)

---

## 6. SETTINGS Screen

**Orientation**: Lay device flat (face up)  
**Display rotation**: 0  
**Background**: `#1A1A1A`  

### Layout

```
┌────────────────┐
│    SETTINGS    │  y=6   DM Mono 8px / #2A2010
│────────────────│  y=20  Divider
│  LONG BREAK    │  y=26  DM Mono 8px  (orange if selected)
│    15 min      │  y=38  DM Mono bold 20px
│────────────────│  y=66  Divider
│  TIMER MODE    │  y=72  DM Mono 8px  (orange if selected)
│   DIGITAL      │  y=84  DM Mono bold 18px
└────────────────┘
```

### Setting Items

| # | Item | Options | Default |
|---|------|---------|---------|
| 1 | LONG BREAK | 15 min / 20 min | 15 min |
| 2 | TIMER MODE | DIGITAL / CIRCLE | DIGITAL |

**Selected item**: Highlighted in `#FF9000` (orange) with subtle background `#1A1400`

### Button Operations

| Action | Condition | Result |
|--------|-----------|--------|
| Short press | Released within 0.6 sec | Change value of selected item |
| Long press | Held ≥ 0.6 sec then released | Move cursor to next item |

All settings are saved to NVS and persist across power cycles.

---

## Common Specifications

### Color Theme

| Screen | Background | Accent |
|--------|-----------|--------|
| BOOT / HOME | `#0D1B2A` | — |
| FOCUS (focus) | `#120808` | `#DD5555` |
| FOCUS (break) | `#081208` | `#55EE88` |
| LONG BREAK | `#060E18` | `#3A80C0` |
| TODAY | `#1E1040` | `#9060DD` |
| SETTINGS | `#1A1A1A` | `#FF9000` |

### Progress Bar / Donut Colors

| Screen | Fill Color | Track Color |
|--------|-----------|------------|
| FOCUS | `#DD5555` | `#491C1C` |
| BREAK | `#55EE88` | `#1C4F2D` |
| LONG BREAK | `#3A80C0` | `#132A40` |

Track color = fill color with each RGB channel divided by 3.

### Typography

| Size | Usage |
|------|-------|
| DM Mono bold 28px | Time (HOME) |
| DM Mono bold 26px | Countdown timer |
| DM Mono bold 36px | Session count (TODAY) |
| DM Mono bold 20–22px | Logo, setting values |
| DM Mono 16px | Date, screen labels |
| DM Mono 9px | Phase labels, section headers |
| DM Mono 8px | Hints, sub-labels |

### Screen Refresh Timing

| Condition | Affected Screens |
|-----------|-----------------|
| Every second (timer active) | FOCUS, LONG BREAK |
| Every 10 seconds | HOME |
| On force-redraw flag | All (state transitions, button press) |
| On timer complete | FOCUS, LONG BREAK |

### Weather Icons (drawn with primitives)

| WMO Code | Weather | Icon Description |
|----------|---------|-----------------|
| 0 | CLEAR | Yellow circle + 8 radial lines |
| 1–3 | CLOUDY | Small sun (upper-left) + white cloud |
| 4–48 | FOG | 5 horizontal grey lines |
| 49–67 | RAIN | Grey cloud + 3 blue teardrops |
| 68–77 | SNOW | Grey cloud + 3 snowflake crosses |
| 78–82 | SHOWER | Same as RAIN |
| 83–86 | SLEET | Same as SNOW |
| 87+ | STORM | Dark cloud + yellow lightning bolt |

### Offline Behavior

| Feature | Online | Offline |
|---------|--------|---------|
| Pomodoro timer | ✅ Normal | ✅ Normal (millis-based) |
| Time display | ✅ NTP synced | ⚠️ Counts from 1/1 00:00 |
| Weather display | ✅ Live data | ⚠️ Shows `Loading...` |
| NTP sync | ✅ | ❌ Skipped |

