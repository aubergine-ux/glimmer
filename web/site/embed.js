// <glimmer-player>: embed a Glimmer cart in any page.
//
//   <script src="https://armd.one/glimmer/embed.js"></script>
//   <glimmer-player cart="sprout"></glimmer-player>                  a bundled demo
//   <glimmer-player src="https://example.com/mygame.png"></glimmer-player>   any cart URL (CORS permitting)
//   <glimmer-player data="..."></glimmer-player>                      a packed cart, as from `share`
//
// Optional: width="640" (default 100%), touch="0" or "1" to force the on-screen controls.
(function () {
  var base = new URL('.', document.currentScript ? document.currentScript.src : location.href).href;

  class GlimmerPlayer extends HTMLElement {
    static get observedAttributes() { return ['cart', 'src', 'data', 'touch', 'width']; }

    connectedCallback() { this.render(); }
    attributeChangedCallback() { if (this.isConnected) this.render(); }

    render() {
      var params = new URLSearchParams();
      if (this.getAttribute('src')) params.set('url', this.getAttribute('src'));
      else if (this.getAttribute('cart')) params.set('cart', this.getAttribute('cart'));
      if (this.getAttribute('touch')) params.set('touch', this.getAttribute('touch'));
      var url = base + 'play/?' + params.toString();
      if (this.getAttribute('data')) url += '#c=' + encodeURIComponent(this.getAttribute('data'));

      var root = this.shadowRoot || this.attachShadow({ mode: 'open' });
      var width = this.getAttribute('width');
      root.innerHTML =
        '<style>:host{display:block;width:' + (width ? width + 'px' : '100%') + ';max-width:100%}' +
        'iframe{display:block;width:100%;aspect-ratio:16/9;border:0;border-radius:6px;background:#0b0a12}</style>';
      var frame = document.createElement('iframe');
      frame.src = url;
      frame.title = 'Glimmer game';
      frame.allow = 'fullscreen; autoplay; gamepad';
      frame.loading = 'lazy';
      root.appendChild(frame);
    }
  }

  if (!customElements.get('glimmer-player')) customElements.define('glimmer-player', GlimmerPlayer);
})();
