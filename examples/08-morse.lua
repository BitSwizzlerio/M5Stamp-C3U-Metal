-- 08-morse.lua: send a message in Morse code on GPIO4 and the RGB LED.
--
-- A dot lasts one unit and a dash three. Inside a letter, the gaps are one unit;
-- between letters, three; between words, seven.
--
-- In the viewer: G4 joins the History, where you can read the dots and dashes.
-- "SOS" is ... --- ...
--
-- Press Stop to end it.
--
-- Try this: change MESSAGE to your name. Or make UNIT smaller and see how fast
-- the History can still show it.

MESSAGE = "SOS HELLO"
UNIT = 120                  -- milliseconds per dot

CODE = {
  A = ".-",   B = "-...", C = "-.-.", D = "-..",  E = ".",    F = "..-.",
  G = "--.",  H = "....", I = "..",   J = ".---", K = "-.-",  L = ".-..",
  M = "--",   N = "-.",   O = "---",  P = ".--.", Q = "--.-", R = ".-.",
  S = "...",  T = "-",    U = "..-",  V = "...-", W = ".--",  X = "-..-",
  Y = "-.--", Z = "--..",
  ["0"] = "-----", ["1"] = ".----", ["2"] = "..---", ["3"] = "...--", ["4"] = "....-",
  ["5"] = ".....", ["6"] = "-....", ["7"] = "--...", ["8"] = "---..", ["9"] = "----.",
}

gpio.output(4)

function signal(on, units)
  gpio.write(4, on)
  if on then led(25, 25, 25) else led(0, 0, 0) end
  delay(units * UNIT)
end

function send(text)
  for letter in text:upper():gmatch(".") do
    if letter == " " then
      delay(4 * UNIT)                   -- 7 between words: 3 have passed already
    elseif CODE[letter] then
      print(letter, CODE[letter])
      for symbol in CODE[letter]:gmatch(".") do
        signal(true, symbol == "." and 1 or 3)
        signal(false, 1)
      end
      delay(2 * UNIT)                   -- 3 between letters: 1 has passed already
    end
  end
end

while true do
  send(MESSAGE)
  delay(2000)
end
