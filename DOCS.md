# Okami SF2 Player — Guía de uso

Un instrumento para Ableton Live que toca SoundFonts (`.sf2` y `.sf3`). Con todo en sus
valores de fábrica suena exactamente como el SoundFont original. Cada control empieza en una
posición que no cambia nada.

## Ponerlo en Ableton

1. En el navegador de Ableton: **Plug-Ins → Marguento → Okami SF2 Player** (AU o VST3).
   Arrástralo a una pista MIDI.
2. Elige un SoundFont en el menú de arriba o arrastra un `.sf2` / `.sf3` a la ventana.
3. Elige un instrumento y toca.

Si no suena, revisa la velocidad de las notas del clip. Con velocidad 1–15 casi no se oye, y
el plugin lo avisa con una línea roja.

## Archivo

- **Menú de archivo:** los últimos 12 SoundFonts abiertos, y al final "Open SoundFont...". El
  abierto muestra cuántos instrumentos tiene: `HQ_Orchestral... (195 instruments)`.
- **Arrastrar y soltar** un `.sf2` o `.sf3` lo abre.
- **Archivo movido o renombrado:** el proyecto lo sigue recordando y el plugin dice en rojo
  cuál no encuentra. Vuelve a elegirlo y regresa todo.
- **Memoria:** solo se cargan las muestras de los instrumentos elegidos. Una colección de
  500 MB ocupa decenas de MB por pista. Al cambiar de instrumento puede haber un instante de
  silencio mientras se leen sus muestras.

## Instrumentos

- **Lista por familias** cuando el SoundFont sigue el estándar General MIDI: Piano,
  Chromatic Percussion, Organ, Guitar, Bass, Strings, Ensemble & Choir, Brass, Reed
  (Sax, Oboe, Clarinet), Pipe (Flutes), Synth Lead, Synth Pad, Synth Effects, Ethnic,
  Percussive, Sound Effects y Drum Kits. Los demás SoundFonts se agrupan por banco.
- **El número en gris** después del nombre es su lugar MIDI: `[040]` es el programa 40 del
  banco 0, y `[1:025]` es el programa 25 del banco 1 (las variaciones). Sirve para cambiar de
  instrumento con un Program Change desde un clip.
- **Buscar:** escribe parte del nombre ("flute") o una familia ("brass"). Enter elige el
  primero; Esc borra la búsqueda.
- **★ Favoritos:** la estrella marca el instrumento elegido. Los favoritos salen arriba de la
  lista, en todas tus pistas y proyectos. Otro clic lo quita.
- **▶** toca una nota corta del instrumento (Do central, o el centro de su rango).
- **`<` `>`** pasan al instrumento anterior o siguiente de la lista.
- **El teclado** ilumina las notas que llegan, toca al hacer clic (más abajo en la tecla =
  más fuerte) y oscurece las teclas que el instrumento no tiene.

## Gain y Tone

- **Gain:** volumen de salida, de −24 a +12 dB. 0 dB es el original.
- **Tone:** corta los agudos, para el siseo de grabaciones viejas (por ejemplo el roce del
  arco en el violín). Hasta la derecha está **Off**. Entre 5 y 8 kHz suaviza el siseo sin
  apagar el sonido; abajo de 3 kHz ya suena opaco.

## Layer (segundo instrumento)

- **Off:** solo suena el instrumento principal.
- **Layer:** un segundo instrumento suena en cada nota junto al principal (por ejemplo
  cuerdas + piano).
- **Split:** las teclas abajo de la nota de split tocan el segundo instrumento y el resto el
  principal (por ejemplo bajo a la izquierda, flauta a la derecha). El teclado pinta en azul
  la zona del segundo instrumento.
- **Nota de split:** de C#-1 a G9, C4 por defecto. Se cambia con − / + o arrastrando.
- **Level:** volumen del segundo instrumento, de −30 a 0 dB.
- El segundo instrumento sigue los pedales, el pitch bend y los controladores del clip.

## Reverb y Chorus

Cada SoundFont dice **cuánto** manda cada instrumento a la reverb y al chorus; el plugin pone
**una** reverb y **un** chorus compartidos (como una mesa de mezcla). El encabezado de cada
panel dice lo que manda el instrumento elegido: `sends 10 %` o `sends nothing`.

- **✓** prende o apaga el efecto.
- **Presets de reverb:** Default (el de FluidSynth), Room, Studio, Hall, Church, Cathedral.
  **Presets de chorus:** Default, Subtle, Wide, Ensemble. Mueven las perillas; si las cambias
  a mano dice "Custom".
- **Reverb:** Level 0–100 %, Room (tamaño de la sala), Damping (cuánto se apagan los agudos de
  la cola), Width (amplitud estéreo).
- **Chorus:** Level 0–200 %, Rate 0.1–5 Hz, Depth 0–20 ms, Voices 1–10.
- **Instrumentos sin envío:** muchos no mandan nada (en la colección HQ, las cuerdas). Para
  darles reverb, sube el **CC91** en el clip (hasta +20 %; el **CC93** hace lo mismo con el
  chorus) o pon una reverb de Ableton en la pista.

### Effects to separate outputs

Con esta casilla, la reverb y el chorus del plugin se apagan y lo que cada instrumento manda
sale **seco** por dos salidas aparte, "Reverb Send" y "Chorus Send", con las proporciones del
SoundFont. La salida principal queda sin efectos. En Ableton:

1. Crea una pista de audio.
2. En **Audio From** elige la pista del plugin y luego **Reverb Send**.
3. Pon **Monitor** en **In** y agrega una reverb al 100 % wet.

Varias pistas pueden mandar a esa misma pista y compartir una reverb.

## PLAY

En sus valores de fábrica (x1, Linear, 0, Poly, Off) no cambian nada.

- **Attack:** cuánto tarda en entrar la nota, de x0.25 a x4 del tiempo original.
- **Release:** cuánto tarda en apagarse al soltar la tecla, de x0.25 a x4. Más corto también
  acorta colas con ruido.
- **Velocity:** de −100 a +100. Positivo hace más fuertes las notas suaves; negativo, más
  suaves. **Linear** = como se tocan.
- **Transpose:** de −24 a +24 semitonos. Usa las muestras de la nota resultante, como si
  tocaras otra tecla.
- **Fine:** afinación fina de −100 a +100 cents.
- **Bend:** rango del pitch bend, de ±1 a ±24 semitonos (±2 es el estándar).
- **Mode:** **Poly** (normal), **Mono** (una nota a la vez; cada nota empieza de nuevo),
  **Legato** (una nota a la vez; si las notas se tocan unidas, la siguiente no vuelve a
  atacar, ideal para violín, flauta o sax).
- **Glide:** deslizamiento de una nota a la siguiente, de 0 (Off) a 2000 ms. En Legato solo
  entre notas unidas.
- **Rueda de modulación:** agrega vibrato, como define el estándar SoundFont.

## Guardado

El proyecto de Ableton guarda el archivo, los dos instrumentos y todas las perillas. Al
cambiar de archivo o de instrumento, Ableton marca el proyecto como modificado y pregunta si
guardar al cerrarlo. Los favoritos y la lista de archivos recientes se guardan en el plugin,
para todos los proyectos.

## Límites

- Frecuencias de muestreo de 8 a 192 kHz.
- Hasta 256 voces a la vez (los instrumentos orquestales usan 4–6 voces por nota).
- Los `.sf3` están comprimidos con pérdida: suenan muy parecido a su `.sf2`, no idénticos.
- La ventana se puede agrandar desde la esquina inferior derecha.
