# ThomasEditor

Plugin propre à PropHunt, **Editor-only**, servant de façade MCP minimale vers les APIs Editor natives UE 5.8.

## Surface v0.1

- `editor_status`
- `blueprint_summary`
- `blueprint_patch`
- `recent_messages`

Le plugin ne contient aucune logique de jeu, aucun module runtime, aucun asset et aucun serveur réseau propre.
Il utilise Remote Control sur `127.0.0.1:30010`, restreint les clients au loopback, interdit les commandes
console/Python distantes, autorise uniquement ses quatre fonctions et génère un token éphémère sous
`Saved/ThomasEditor/session.token`.

Documentation interne : `SourceArt/Docs/THOMAS_EDITOR_PLUGIN.md`.

Serveur MCP : `Plugins/ThomasEditor/Resources/python/thomas_editor_mcp.py`.
Il utilise uniquement la bibliothèque standard Python et n'installe aucun paquet global.

## Démarrage

1. Ouvrir directement `C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject` avec `K:\UE58`.
2. Ouvrir ou relancer Codex depuis la racine PropHunt : `.codex/config.toml` enregistre automatiquement le
   serveur MCP uniquement pour ce dépôt approuvé.
3. Appeler d'abord `editor_status`, puis lire un Blueprint avant toute demande de mutation.

Le serveur MCP ne démarre pas Unreal et ne cherche aucun autre projet. Si l'éditeur PropHunt ou son token de
session n'est pas disponible, il répond `session_unavailable` ou `editor_unreachable`.
Un Blueprint sale est refusé ; les chemins de classes doivent être exacts et un patch en erreur est restauré.
La configuration utilise volontairement le chemin canonique absolu de PropHunt ; il faut l'actualiser si le
checkout est déplacé.

## Organisation

- `Public/Bridge` : quatre fonctions Remote Control publiques.
- `Private/Bridge` : délégation et authentification.
- `Private/Blueprint` : lecture et mutation Blueprint.
- `Private/Infrastructure` : cycle de vie, token, allowlist et logs.
- `Resources/python` : façade MCP stdio.

Un seul module Editor est volontairement conservé : plusieurs modules Unreal ne sont pas justifiés par cette
surface réduite.
