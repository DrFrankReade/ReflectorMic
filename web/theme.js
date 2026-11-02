export function initTheme(document, storage, media) {
  const select = document.getElementById('theme');
  let theme;
  try { theme = storage?.getItem('reflectormic-theme'); } catch { /* Storage can be disabled. */ }
  if (theme !== 'light' && theme !== 'dark') theme = media?.matches ? 'dark' : 'light';
  const apply = value => { document.documentElement.dataset.theme = value; select.value = value; };
  apply(theme);
  select.addEventListener('change', () => {
    apply(select.value);
    try { storage?.setItem('reflectormic-theme', select.value); } catch { /* Theme still works this visit. */ }
  });
}
