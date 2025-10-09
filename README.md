# Ternary-image-codec
Structures & API pour encodage 8K (preversion qui peut le plus peut le moins) en balanced ternary
RS(GF(27)) avec entrelacement 9-bandes, option 2D, UEP, super-trame auto-décrite
Pilote coset + balise clairsemée (sans trit de parité local)
ternary_codec_full.hpp
Encodage balanced-ternary 8K :
 - Mots 27 trits = 9 symboles GF(27)
 - Profils RS(26,k), entrelacement 1D/2D, UEP
 - En-tête de super-trame, pilote coset et balise clairsemée
 - Mode RAW
 - Conversion 2 pixels ↔ mot 27 trits

TODO connecteur format de fichier , images , video 

non fonctionnel ici ou très partiel , mais version en cours de progrès hors repos optimisée pour 1 pixels par mots trits balanced ternary 13 trits ,reste du Word27 en security et options
