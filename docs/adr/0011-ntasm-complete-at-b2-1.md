# ADR 0011 — NTASM complet livré à B2.1

- Statut : accepté sur demande explicite de l'utilisateur.
- Date : 2026-09-04.

L'utilisateur exige que B2.1 soit NTASM complet. L'ordre « NTASM v0 après
NX/Nova/OS » de l'ancien plan est remplacé.

B1 termine S0; B2 prouve l'auto-hébergement du bootstrap; B2.1 livre NTASM v0
typé complet, son outillage et son build normal sans NASM. Le contrat est
[`ntasm-completion.md`](../ntasm-completion.md), exigences A1 à A10.

Les fonctions avancées restent dans v0 sans agrandir implicitement S0.
La cible initiale reste x86_64; le cœur et le backend CPU restent partagés.
Le producteur NTASM doit pouvoir être construit avec les outils maison
disponibles sans attendre la livraison de NX, Nova ou de l'OS.

Renommer un prototype ne satisfait pas cette décision. B2.1 reste non livré
tant que toutes ses exigences ne sont pas prouvées.
