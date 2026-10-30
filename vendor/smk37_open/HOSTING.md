# SMK37 web hosting

The [SMK37 OPEN workbench](https://open.m-vave.com/smk37-workbench) opens the Pro / Elite model selector. Its entry file is `smk37-workbench.html`; the production server serves it at `/smk37-workbench`.

Set `FM1_SOURCE_URL` to the HTTPS download of the exact matching source/development kit, including your changes, licenses and build/install scripts. Make the archive accessible to recipients before publishing its workbench. The variable name is shared with FM1; use the archive for this product.

Build each required model with `build.bat pro --package` or `build.bat elite --package`. Keep the complete `build/official-web/<model>/SMK37-Open-Source.html` as an offline download.

Create a static site for each model:

```text
node tools/Build-Hosted.cjs <complete-workbench.html> <new-output-directory>
```

Upload the entire output directory without changing its relative paths. Upload assets before the root `index.html` and `smk37-workbench.html` entries. Use separate directories for Pro and Elite, and keep existing versioned downloads accessible.

Check that each entry opens, identifies the intended model and downloads its matching source archive and offline HTML. HTTPS is required for browser MIDI access on a hosted site. For offline use, open the complete HTML in desktop Chrome or Edge.

Redistributions must retain the applicable license and attribution files. Build and package contents are described in the included manifests; these do not certify hardware operation or third-party firmware compatibility.
