# JIC Content Library

This directory holds the **starter documents** committed to the repo. At
runtime the library lives in the `jic-sources` named volume — these files
are seeded into that volume by the content fetcher, and the full curated
catalog is downloaded from [`sources.yaml`](../../sources.yaml):

```bash
# populate the library volume (seeds these files + downloads the manifest)
docker compose --profile fetch run --rm content-fetch

# or download to this directory directly for a native (non-Docker) run
./helper-scripts/fetch-source-data.sh
```

Useful manifest commands:

```bash
helper-scripts/fetch-source-data.sh --list        # show the catalog
helper-scripts/fetch-source-data.sh --validate    # lint the manifest
helper-scripts/fetch-source-data.sh --category 200_Medical   # one category
```

Add your own documents to a running stack:

```bash
docker compose cp my-manual.pdf jic-server:/app/public/sources/100_Survival/
```

## Categories

| Folder | Scope |
|---|---|
| `100_Survival` | Preparedness, wilderness survival, land navigation, CBRN |
| `200_Medical` | First aid, austere medicine, surgery, dental, mental health |
| `300_Food` | Canning/preservation, gardening, agriculture |
| `400_Engineering` | Water supply & purification, solar/wind power, construction |
| `500_Comms` | Ham radio, emergency frequencies, interoperability guides |
| `600_Education` | Open textbooks |
| `700_Social` | Civic documents, community resilience |
| `800_Software` | Freely-licensed technical references |

## Bulk / external collections (not auto-fetched)

- [Kiwix ZIM library](https://library.kiwix.org) — offline Wikipedia, WikiHow, iFixit, medical wikis
- [OpenStreetMap extracts](https://download.geofabrik.de) — offline maps
- [Kolibri / Khan Academy](https://learningequality.org) — structured courses
- [Survival-Data corpus](https://github.com/PR0M3TH3AN/Survival-Data) — community collection (check per-file licenses)
- [Standard Ebooks](https://standardebooks.org/bulk-downloads) · [Project Gutenberg](https://www.gutenberg.org) · [Internet Archive](https://archive.org)

**Licensing**: every document added here or to the manifest must be
public-domain, Creative Commons, or explicitly redistributable — record
the license in `sources.yaml`. See [docs/1400-sources.md](../../docs/1400-sources.md).
