# Guía estética

Toda pantalla del juego usa esta paleta. No se inventan colores nuevos: si hace
falta un tono más, se saca oscureciendo o aclarando uno de los cinco base.

## Paleta base

| Uso | Hex | RGB |
|-----|-----|-----|
| Base de paredes | `#23314A` | 35, 49, 74 |
| Detalle de paredes | `#BF2078` | 191, 32, 120 |
| Suelo | `#AAADB3` | 170, 173, 179 |
| Enemigos | `#26BF21` | 38, 191, 33 |
| Sombra de detalle en sprites | `#6A3553` | 106, 53, 83 |

## Reglas

1. **El verde es solo para enemigos.** Nada del entorno lo usa. Es la única
   señal de peligro que tiene el jugador, así que pierde valor en cuanto
   aparece en una pared o en el HUD.
2. **El rosa fuerte se usa con moderación**, como acento sobre el azul: juntas,
   trazas, nodos. Nunca como color de relleno de una superficie entera.
3. **Los tonos derivados salen de multiplicar un color base**, no de elegir uno
   nuevo a ojo. La rampa de sombreado por distancia ya lo hace automáticamente.
4. **El suelo lleva líneas** que se alejan hacia el horizonte. El techo no: se
   oscurece hasta casi negro para que la vista tenga un arriba y un abajo
   distinguibles sin gastar detalle.

## Cómo está implementado

Los colores no se escriben sueltos por el código. Viven en una paleta única en
`src/engine/Textures.h`, donde cada color base aparece ya multiplicado por los
ocho niveles de sombreado por distancia. El renderizador solo maneja índices.

Consecuencia práctica: **para cambiar un color del juego se toca un sitio**, el
array `BASE_RGB` y las constantes de al lado. Para agregar un material nuevo se
agregan sus cuatro colores y su patrón, y nada más se entera.

## Materiales de pared

Cada sala del nivel recibe un material distinto, de modo que dos salas contiguas
nunca se ven iguales.

| Carácter | Material | Lectura |
|----------|----------|---------|
| `+` | PANEL | Placas grandes con juntas y nodos rosa |
| `-` | CONDUIT | Conductos verticales con datos en rosa |
| `\|` | GRID | Rejilla fina gris sobre azul, cruces en rosa sombra |
| `#` | genérico | Pasillos; usa PANEL |
