# Static site for the Tesla account link

These files are published on a **public HTTPS domain you control** (any static host works). They contain nothing private.

| File | Purpose |
|---|---|
| `.well-known/appspecific/com.tesla.3p.public-key.pem` | The **public** key of your Tesla developer app. Tesla fetches it from exactly this path when you register the domain and when a car owner approves the app's key. |
| `callback.html` | The page Tesla sends the browser to after a sign-in. It forwards the one-use code to the board's own address on the home network (carried in `state`) and refuses any address that is not a private one. |
| `index.html` | A blank placeholder page. |
| `.nojekyll` | Stops GitHub Pages from hiding the `.well-known` folder. Harmless elsewhere. |

**Never put the private key here.** Only the public half is published; the private key stays outside the repository
(for example in `~/.wattsync/`) and is sent only to your own board by the setup tool.

To use it: replace the public key with your own, upload the folder to a static host (for example Netlify Drop or Cloudflare Pages), set the
app's Allowed Origin to that domain and add `https://<your-domain>/callback.html` to its Allowed Redirect URIs.
The full procedure is in [`docs/tesla-fleet-api.md`](../docs/tesla-fleet-api.md).
