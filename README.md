# PropHunt

Jeu multijoueur asymétrique développé avec Unreal Engine 5.8.

- 1 Hunter en première personne.
- 1 à 4 Props en troisième personne.
- Combat de mêlée et capacités, sans arme à distance.
- Objectifs coopératifs, transformation en objets, capture et évasion.
- Distribution PC via Steam uniquement.

## Contenu du dépôt

Ce dépôt contient le projet reproductible : configuration Unreal partagée, code C++ et assets Unreal validés sous `Content/PropHunt`.

La documentation interne et les fichiers de création externes sont versionnés séparément et exclus de ce dépôt public. Les expériences temporaires vivent sous `Content/Developers/<Utilisateur>/` et ne doivent jamais être référencées par le contenu de production.

## Architecture

Le gameplay, l'autorité réseau et les validations sont écrits en C++. Les valeurs, animations, sons et variantes visuelles sont exposés aux Blueprints et aux Data Assets. Le jeu peut instancier des acteurs et assets cuisinés pendant une partie, mais ne crée pas de nouveau contenu procédural au runtime.
