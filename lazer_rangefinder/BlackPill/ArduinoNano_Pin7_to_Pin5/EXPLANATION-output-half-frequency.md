# Outputting half of the average pulse frequency on a digital pin

## What you already have

From the rising-edge measurements you get an **average period** $T_\mathrm{avg}$ (in seconds or microseconds). The **average input frequency** is:

$$
f_\mathrm{avg} = \frac{1}{T_\mathrm{avg}}
$$

You want the **output** to run at **half** of that frequency:

$$
f_\mathrm{out} = \frac{f_\mathrm{avg}}{2}
$$

So the **output period** must be:

$$
T_\mathrm{out} = \frac{1}{f_\mathrm{out}} = \frac{2}{f_\mathrm{avg}} = 2\,T_\mathrm{avg}
$$

So the output waveform is **twice as long** as one average input period.

---

## Square wave on one digital pin

If the output is a **square wave** (alternating HIGH and LOW for equal time), one full output cycle is HIGH for half the time and LOW for half the time. Then:

| Quantity | Expression |
|----------|------------|
| Full output period | $T_\mathrm{out} = 2\,T_\mathrm{avg}$ |
| Time from one **toggle** to the next | $\dfrac{T_\mathrm{out}}{2} = T_\mathrm{avg}$ |

So in words: **flip the output pin every** $T_\mathrm{avg}$ (using the same time units as your period measurement, e.g. microseconds). That produces an output frequency of:

$$
f_\mathrm{out} = \frac{f_\mathrm{avg}}{2}
$$

This is the usual mental model:

$$
\text{toggle interval} = T_\mathrm{avg} \quad\implies\quad f_\mathrm{out} = \frac{f_\mathrm{avg}}{2}
$$

---

## How to do it in software (conceptually, no code)

1. **Keep measuring** periods and updating your rolling average $T_\mathrm{avg}$ as you do now.

2. **Generate the output** without blocking `loop()`:
   - Remember whether the output is currently HIGH or LOW.
   - Remember the **next time** you should toggle the pin (using the same timebase as for input, e.g. `micros()`).
   - On each pass through `loop()`, if “now” is at or past that time: toggle the pin, then set the **next** toggle time to **now +** $T_\mathrm{avg}$ (using your latest average).

3. **When** $T_\mathrm{avg}$ **changes** (because the buffer updated), update the **toggle step** to the new $T_\mathrm{avg}$. The next toggle can use the new value; a small phase jump is normal unless you add extra smoothing.

4. **Startup**: until you have at least one valid average (or until the buffer has enough samples you trust), hold the output in a defined state (e.g. LOW) and do not start toggling yet.

---

## Practical limits on a Nano

- **`micros()`** on typical AVR Arduino has about **4 µs** resolution, so very high output frequencies are quantized.
- **Very low** frequencies (long $T_\mathrm{avg}$) are fine as long as you use a wide enough integer type for times and handle `millis()` / `micros()` overflow (unsigned subtraction in the usual way).
- If the **average changes quickly**, the toggling rate changes quickly; that is expected.

---

## Alternative: hardware PWM / timers

For a **stable** rate that does not depend on polling `loop()`, you can configure a **hardware timer** or **PWM** and set the period from your computed $T_\mathrm{out}$ or half-period. That is more work but gives cleaner timing at higher frequencies. For slow signals, bit-banging with `micros()` is usually enough.

---

## Summary

| Goal | Formula |
|------|---------|
| Output frequency | $f_\mathrm{out} = \dfrac{f_\mathrm{avg}}{2}$ |
| Output period | $T_\mathrm{out} = 2\,T_\mathrm{avg}$ |
| Square wave: time between toggles | $T_\mathrm{toggle} = T_\mathrm{avg}$ |

That is the relationship you implement on the output pin.

---

## Plain text (if math does not render)

```
f_avg = 1 / T_avg

f_out = f_avg / 2

T_out = 1 / f_out = 2 / f_avg = 2 * T_avg

Square wave: toggle every T_avg  →  f_out = f_avg / 2
```
