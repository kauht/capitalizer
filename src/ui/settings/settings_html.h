#pragma once

namespace ui {

inline constexpr wchar_t kSettingsHtml[] = LR"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
:root { color-scheme: dark; }
* { box-sizing: border-box; margin: 0; padding: 0; }
html, body { height: 100%; }
body {
  font-family: 'Segoe UI Variable Text','Segoe UI',system-ui,sans-serif;
  color: #e6e6ea; background: transparent; overflow: hidden;
  user-select: none; -webkit-user-select: none;
  display: flex; flex-direction: column;
}
.titlebar { flex: none; height: 42px; display: flex; align-items: center;
  padding: 0 0 0 16px; gap: 12px; background: rgba(22,22,27,0.30); }
.tb-icon { width: 30px; height: 30px; border-radius: 8px; flex: none; }
.tb-name { flex: 1; display: flex; align-items: center; font-size: 14px; font-weight: 600;
  color: #ededf2; letter-spacing: .2px; }
.wc { display: flex; align-self: stretch; }
.wc-btn { width: 46px; display: grid; place-items: center;
  color: #c4c4ce; cursor: pointer; transition: background .1s, color .1s; }
.wc-btn:hover { background: rgba(255,255,255,0.09); color: #fff; }
.wc-btn.close:hover { background: #e2453a; color: #fff; }
.wc-btn svg { width: 12px; height: 12px; display: block; }
.panel { flex: 1 1 auto; min-height: 0; display: flex; flex-direction: column;
  padding: 12px 22px 8px; background: rgba(22,22,27,0.30); }
.rows { display: flex; flex-direction: column; }
.row { display: flex; align-items: center; justify-content: space-between;
  padding: 12px 0; border-bottom: 1px solid rgba(255,255,255,0.05); }
.label { font-size: 13px; color: #d6d6db; }
.hk { min-width: 170px; text-align: center; font-size: 12.5px; color: #e6e6ea;
  padding: 8px 12px; background: rgba(255,255,255,0.055);
  border: 1px solid rgba(255,255,255,0.09); border-radius: 8px; cursor: pointer;
  transition: background .12s, border-color .12s; outline: none; }
.hk:hover { background: rgba(255,255,255,0.09); }
.hk.capturing { border-color: #4c9bf5; background: rgba(76,155,245,0.14); color: #bcd8fb; }
.toggle { width: 42px; height: 24px; border-radius: 999px; background: rgba(255,255,255,0.16);
  position: relative; cursor: pointer; transition: background .16s; outline: none; }
.toggle .knob { position: absolute; top: 3px; left: 3px; width: 18px; height: 18px;
  border-radius: 50%; background: #fff; transition: transform .16s; box-shadow: 0 1px 2px rgba(0,0,0,.4); }
.toggle.on { background: #2c7df0; }
.toggle.on .knob { transform: translateX(18px); }
.statusbar { flex: none; display: flex; align-items: center; justify-content: center;
  padding: 7px 16px; border-top: 1px solid rgba(255,255,255,0.06);
  background: rgba(255,255,255,0.015); }
.hint { font-size: 11px; line-height: 1; color: #74747e; }
</style></head><body>
<div class="titlebar" id="titlebar">
  <img class="tb-icon" src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEgAAABICAYAAABV7bNHAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAABj7SURBVHhe7ZvpU1tXmsbpyUxnceLY8YKxjdkXs4kdDIh9EZuQQEICiUVC7CDEjkBgbAw2xhsG7yvYcew4TjqZ7J3pSWc6ne58m6qempmq+RO6aqanpqZqaqr9TL3vuVcSSrqr2sGp+cCpeupK954r3fPT875nuVd+fltlq2yVrbJVtspW2Spb5ZnLysrKricf/r3q3Y//YeLJp7+58vjDb9aefPLt3ccffbP+5KPfrr/z8bdrjz/8Leudj0nfrj3+4Ju1Rx98s/bW+1+vPXjvV5K+Wrv3+Jfr64+/ZN0lPfpy/fajX6zffviL9VsPv1i/9eCL9RsPfr5+/f7n967d//TelfVP7l1Z/3h95db7pKtX1j6cWn/0qWptbW2X73X+6GV9fT3uvU9+fev+k69+f/Phl1i5+xkWVt/F7LmHOHr2LdbMmQeYXnoTrtP3MbV4D5On1lnOhbsYP3Ebo3M3MXLsBoaOXsPgzFU4pi/DPrWKPudF9IxfQPfYeXSPnkPX6Dl0DJ9B++Bp2ByLsA6cgsV+Ehb7Alr6TqC59wRa+xe4/uixa5g9fff3y9eeXD9z8Vq073X/KOWtJ59Prj/56n/O3/oEdtcltNoXYO6eRUPnDAztLujbnNBZJ1BnGUdtyxi0zSMsTRNpGDXmIagbHahqGGBVGvtRUd+Lcn0PVLpulNZ2oljTLsmGInUbCqosyK9sQV5FM3JJ5U1QlpuRXdaIrBIjjhQbkFlUj8xiI4o1nWgfWsLs0vp/L60+GPa9/udaHrz7xdqdd36N7ollBtIoqaFjGgbbFPRWJ4OpayUwo9CYh6E2DbKqGwmIHVVGOyoMfQyEVFYnoJRIUArVbcivahWqbGEgBCNHZWIg2aUyFCNDySAV6pFeoEN6fh3S8uuQmlfLoMip8+fXrvm247mUe29/ukpwWuwLMLZPw9juElDaJtkxsltkKNWNDlS6YfRCpSMYXQKGVnJHjY2BFFRbGEgeASlvYhg5ZSZklzZ4QEgQZACpeVqk5mqRkitvNZK0SFFqkJxTg6ySRgzPXsfs4q0l3/Zsarl195H2zfd/w7FPUOoJijuERjl0ZCgcNoZ+lNf3iZCp60KJVoRNUY0PDK9wUarMDCWLoYiQISgCiACQrKxBUo4aidnVSMyukrbVSJLk+5qUU9aE0WM3MDG3rPJt16aUmJiYn95+8Nm/jc/fhc4yAT27RUCRcwqDYbcQGI9bSqR8Ipxi5Vwig5GByGFDbqHQySwyeLmlVoDJUUuNrkJiVhUUWZVIOEKqkFQJBSlLiOrIgEhq0whGZi79c15e3l/7tu8Hl5XLd9VX73+BepsLda3jHEZqk4BS3eDrmB7JMR1eYMg1Vq8QakZOmVnKJQ04IiVZziUEpUDOI1okKzVIzFYLKBIEBpNZgfiMcsRlqBCXTirj97SfxbAIpFBqXh16Jy5ifHq52rd9P7hcvv3efeq+CQq5parBwU4RbhFQyrj32QimQE1QpHCqEGCUqiZkUxiVNCCToBTWI53dIkGRcgmBoRySmFXNjY3PLEc8wZCVrkJsWikrhpRawq8ZVoaKYdE55C6CSp9T3zaN4ZnVNd/2/dDyVxdv/uxfO0bOosoonEJgVPpe4RQKIW0HimS3EBh2iwXZZWakFeiRmq9DSl4dkpW1SM6tZTAURul0LE8kXAai1AhASo07zyiOVCE2XYVwRT5C43IRGqdEaFwOQmKFopIKcTilmCUglSE2XUgGRY4iFxXVdKJv/Oy/UJt8G/nMpc3Y5r+48ui/DO3TbjDULRMYAaVdQHGHEeWYVmQWN6CqvgedQ/PoHDqJjqEFdA4vwNQxwU5JztXyNiVP5BgZCLnGDSerClHJxeyc6vpOGNvG0NA+AaNtHPWWUdQ09CImtRgRinxEJxexPKDIXWUsDr/McmQUGWEbWPhPi8Wyx7edz1yczvmYE+fuQ9M0KvJLbZdwjAyFEm+1laGI8Uoz0gsNaLCN4+6Tr3Hznd/i+tvf4NqjX+Pqw69x68m3mF68LXobZQ27JkmGkiWgyIpILISy1IDp03cxdeYxhuffxODcfTjm7sFxfB3OpbfRPXoa4QlKRCYVfAcSi0CRm9JVSFZq0dx97I89PRPhvu185uJynVTMnl7jvFNKzpHheLmFwOSoaOxiRlapiUNm8dJjXHrwNZZufo4ztz7H6Ruf4dT1TzF/5WNcfutXT9UNPYhMKoKCoQgwolcSYjhlBpy4+PipY+5NtI1dgW3sCtrGLsM6cgnW0UtoHlrF6Pz605ScSg47CjcZkgeUyFHkqIQjVTB1TMPWMxbr285nLiPO+YSp+Vsor+9HsabDnWMEmBYGw0m3VCTejCID0vJqcerSOzh7++8YzCLBufYJFq5+jBOXP8TZO7+AvnUQwTE5iKfeKFNKwlIPFK4oQHaJHnPLjxhOh/M6OiauwjbuAdQ6vIK2sWuwuy4jLO4IA4pMzEdUUgGDkmFxfkotYcWml6PBNoWeHtcmAhqZT5g4fh2ltd1u1xAYZbl3bySPXeqRlq/j8crchbdw+ubPsXDlI8xf/ghzlz7E8dW/xezF92n/U42p92lgVIbIERkqTsQUBuEJ+cgs0OL4hYcYPPEAnZPX0eG8BtvYZbSNXoJ1ZBUtQxfRMrSKoeN3kJJTAf/gBITHKxGhyGNIsghWdLLHVTFpZTC2PQdAo7NXUaTpYNe4wZQKMDR2oW6awFBoJeVokJBZjqNn72Ph6mc4vvoBQ5lZ/hlmLrwH1/l3eX+1oevp/rAU6cJFQqVeKlVZjdlzDzA0/104luEVtAwuo3lwBYOzt5GSpcL2vaEIPpyJsLgcRCTkIlKRx6AiEvIQqfCAikwswOHUUuEgxyYCGhx0KYZnLqFAbeNwyipt5DBKp/FLgQSGeqNcLcOhfBKXVgLX6TUcW/2IoUyffxdTZ5/AeeYdjJ9+G7OrH6Jc1w7/kCS+cIJE4ZaYJcAOLzxE5+QNCc4VWCikhlbQ5LiAJsdF2GduIjlLhW27DiEwMg3BhynEshGekCvByRWvCZLbUQWITimFsW1ycwHZR+YTBl2ryKu0MBxyjQAjxi8EhrpsMeKtRlxGBWJSiuA8dQczyx8IMEuPGczoqYcYXniAqfM/Q6nWir1BCQhPyENgdCbi00ows3QPIycfueFQYrZQSA0uwzxwHuaBC+ifuYmU7HK88kYgDkakICg6A8ExRxAam4WweCX3aB4JYO6QSy6GwercXEB9gy7FwOQylOUtPKumPCM7hrtoZQ0SpS6aeh+K8+ikfIzN38Tkufcwtvg2Rk4+5JAZPHEf9uP3MHr6MQqrWxjQgYg0HE7Oh2vxDkZOPeKE3D5BcC6jdXgVTY5lmO3n0Nh/Dr2u60hTVuKVnYE4EJ6MQ1HpHF4EKIQgxWVzqIXF5/CW8pLbVQoCVASDdWJzc5B95GhC/8QFZJWZOawopNxwctQbuui4jHJEp5QgUpGLoWPXMLr4mME45u7Dfmwd/bNr6Jm+jYG5N6EsN2G7fwQi4nMwefIWRk+97YZDIdU8dJFd09h/Fsbes+ievIY0ZRVe3nEA+0MTERiZiqBoCZAkhhSbjZDYbN4KWF5OShKA+vuPxvi285kLAxo/zyNjmhaIXCMGdjIY7qozyjnZRiYVIjw+B32uSxg48YCh9B69w2C6XDfR7ryOTtctzlf7guIwMX+DQdomrnngDF6EyX4eDX1nYOw9gy7nNaTlqvHS6/uxLySBQ+tQVJoIr8MZCDqcKb0WkEJis1jsqPgcBsSQEgtQbxlH//AmA+obO8ejY3nUS7lGBiO66DKGQ8mWckpYXBY6aU15+g46p26gY/I6A6Cc0jK8CvPgCi9j9E2cwcji27COXWG1jqwyHAonY+8S6nuWePyTnleDl14PwL5gDxwKL3IQgfFWcAxBEoBkSBRq5KQIhQC06Tmod/QMTzoFHLU7nASYMh6ERacUIzKxEGHxuQiJyUTb8Fm0O29wwy2jl9BCvdDgMgy9Z2DoXYJz8f5Tx4k3YRml45fFcccyO4fg6LtPc/eenq/Fi9sD4B8Uj4OcdzxwaOsLyu0iyUnuUKOcpMiHvnVscwGRg3pGlng2rpAmkDTqZefIcJKLeeRKiZBGtHSRFsdpbnzz0ArMjmU02s+zI3Rdp+Bcesg5qXXkMsPhOgMXpHyzhPru0zzmITgv79iPvUFxOBCWxHlHAMmQ4KRxN++BJkKOJPKRAEQuYifJgDY7SXcNLSJJqZWmBRU84pXXYBhOciF3oxTntAQRHJ0Bc98CTI5lNPSdhbHvLHRdi6htn8fYyTfRP3uX51Etw16u6TuD+p7T0HWeRLPjAtLyPHD2hyWJ0GIY6Tjkdk+atM8DSZbHSQRJCrXn4aC+Ppeic/AUJ1VewSM4qaU8x6GcQ86hQRjBoTgPjslCUFQ6jF3HGUx99yLqOk6ixnocQyfW0Dtzmx0lg2noPwuDBIYAmu3neU3op6/5Y+8hDxzhHgkEQYkk96R+j0QdugZfJ1F+1LWMoGczJ6t2+9GEDsdJzj2ccxgOOccDh0asBEeE1xG+QL3tKOcRarS6dRb2o7fQM30LpoELMA1QD3WWcxGFE8HR2uZg7FlEboUVsWkq7AuOx55DcTgY4Wm0DEcGIUOhOgyRQW5M4N6AwhLyUNc0vPmA2u3zPN6hsNrgHEU+jy/YPXFKtvOh6Ay+aG3rJGqsc6hscqF78iq6Jq9z79Rol3qo7kUGU9c+D431OAzdp3gwGppQiNL6ITS0zyAoOg37QhRe+cbXLakMheDILvM4TcpJbkA00s5FrXloc5c7CFBb3xwvSWx0Tj47Rw4tHqARoKh07m2qGkegapiAZfA8bGOXYOhZYjDe4aRpm2N36ToWkFfZhjBFEbIr21FpcqJ54DxsgycRzJASudGyUzYo/HsAyWEmDSTlHu25AKIcZOk9znOsDXBk53jBoYshix8IS0ZxbR/q2o6hbWQFdR3z0HedYhDsmLbjUFtmUdk8jVrbHJQVFoQpCpGpsqLMOAKtdRZ1tuOwDq+gY+gUgqJS4R+skIAkS5LgyO9lSByScljKPZuYjhAg7WYD6rY7E1q7j/Fik3fOCY//LhzhnlSeChRqutDUv8QAtG2k49BYj7FjCIzK5OTQyquyISQuD2nFTSjWOVBlnmRAuvYTLOvICmyOBQRGJGOvNBaieZhHSbz1APJ1kgfS8wHU7Uxo6Z7lBS2GQ86Jp7sLOW44NNQXuScN+8OSeVBXYRhAfedJVDVPo7plhreUjyqapqBqnEBV8wzSC+oRFJ2F9OJm5Gt6oTKOoqppChqLcJC+cx76jhPsJKt9DgfCErlnIyjUu9HYiBVOPZ2AtAGQezri4yDbZgKyOxOau2Z4scmdlCXnUJfOzpHgUI4gQHsPxaLaNMJuqTBPodw0CVWjU9IEb4vr+vHCSzvw0vZ9yC5rRZlxFOWN41C3uKCRQkzXPof6znnoKNyGltHadwz7QxOw51AsA5IlIHkAiZ4vFYE+eUgAGtxkQN3OBHO7i9dSeNJHoUX3pSQ49AsFUmhJCTQgNIkdVGkcQnnjJMoaxlFqHPPIQC6ZRlx6Ofxe2IYXXn4DO/aGoKS2D1VNLlSaJ1HTMi3yUNsx6NqPQ98+hzrbMViHLqC5e+ZpQEi8e4y0PyzRA8g71LzHRFJvRoA0JgeaNxNQZ+e4wtw+xWsp1JWLm3bkniMCUFS65J4UTs7ULfsHxaFUZ0eJYQzF+hEU64dRJEs3hArTJIMmONt2BfKgcFdABMp0dlQ3u1BhmkB18xQ0rTPQWo6iVoJVaz2KVsc5mDpdCAhJcENyw/FykQxIuEhMYhlQIwFybC4gk40AFfMXkHtEeB1BULRIzDIgulhaQCdARdpeFOmGUVg3yCqoJTlYZcZxTvQvbt+HHf5hrJd3BGD3gSio9AOchyoax1HdNAl18xRqWlzQtk4zIK1lBi0DZ9HQPikgBcV/xz3eDiKJPCQcVLPZgCjEGtuc7CB3eMVSeG0EdCA8BfspvILpl41FoaYbBXWDyNcObJTGjpL6MQb0ys6D2HUgCm/sj8IbARF4ZecB7DkYjTK9nXuzSoJknkR1kxPqZgq9KQalaXGh2X4GDTYnQ6Lv9B4PeSBt7MXotnVNw8DmA2qwTkhLGd7dOgESs2rKP/QrBoQmSoBikK/uRJ52ALk1/cjTkOxCNf0ccuHxeXh11yHsPniYxaACIrHtjYMMSaW3s4MIUpV5AtXmCaibnKhpnoSm1cWuau5fgrHNiYBQBYc2/Uju3ox6MZrUSlMOGZDaaN9cQJbOcYUbkFd4MSB57CMBovGP7KDcqnaGo1T3snLVfcit6eNtoW4IEQn5eG1PMPYExnD9PYdisPtgNIPa9kYg9h48zOFGDqpoGEWVidwkg5pkFxGk1oFz0DYNSrP+ZByISPbq4j1zMp5yxClRbeiHxTK8eSuKBMhoGecxkAzI7SDu4jfmIFoSpQYrK21QqvuQU92NnOoeKEkEq7qH8xCt7hEgSrTU65GokeQ+chS5a29gDDup0jSBcuMIqshNEig3pOYptA6cQUxKIZ/vdg/Nx9zukSatzwMQJWkCROu53oNDDyB5DJTiDjNyQVZpM4PIruxCdhVJgKJtiX6EP+N1/1ABKDh+g9hRgTF4dXcQ/ANjUK63M4jKhlF2U0XDmABFvZ3ZCXPPSb5R4AuI4ATxApo8DhKATJb+zQPU1jaSYGwd51/cA0gKMcpB0uIVzYHkMCMHUPdfqOlBcd0gimoHeFuiG0JZ/TDSCuq556J61OMRFFreoDVnEoMKimNIr+0Oxt7AwyiqtkFnmYbOMoM6ywx01qOobzuGhs45KMtM2LU/ksdEvoB4FC3f8WAH9T0HQFKIeTtIQBIDRRFmqdyTUJgFhCiwMyCSG8jHIwVAHjNFpOD1vSHsMgorqkthSaLXHhEs4aYd/qHYvjsIhyJS+DaRrChFLkKi07FjX5iUgwSgQ15LszIgnotJgHSmzs0DZLMNRRot43+MSioScOhugbQoTi5yQ+LuXoymRS5SYHdgDHbsC+dQ2r43hLc790UwOIYTqnCPhhlKaCK/9kwjEtmR5Co6Z+e+cGzfE4Lte4KF9oZgZ0AEg/zuyqNI0GKNWgCiNfNqQ9//1tY2hvq285mLRqPZ1Wid+AMtt/ICuLwQzqA8ucgXEs+0pYZSwwkANXbDHEoa3HFofs90gRssrfmIz5IA0ud4aWNYpfOauJyc5fAi0WpotaHv3+Pj43f6tvOHlJ+0dEz97kiRAWHSrV2+S+CG9F0nyb+gPIik8JInkGLJVLobIQ3k5CRP9Tg83Mf+jLiOd3cu9Vbet328RNeakluLWtPgP/o28AeXFpvzRlV9P0OR73sTLDekDWFHdt4Y+xvGI1JD5N7FfdxrHZnryA5wO+FPyF3XAyY01msdmq9TLLnS85R1jfbLvu37wUVday0w26Z5PVp2kQeUBIv2e4HaKK/753Tx1AjpV5VXBfiYLL476lXPSxshiM/eKM8xui66DU7bqMQCaE1DUFU15fi2b1NKY+vYb1S13fyF4ksFJH69wVXSfag/IQF4ozbU8YLnW0+I9nsk1xOwPeEkw+HbznHZKFLboDX2f+Xbrk0rqipTWnPn0aeZhXrxuFuCR/yYCUuGJz004CXPe+9zvI5Tg3wU4f09PvXp+HfheRQuPXFGdTMK9NA3jz7NyalM8W3Xphatvstu6TkBgsQXraBH3uhJLtJGaEK5iORjdKHS++89RzRkAxB6tFeqy/L5Dm9IG8BJn8PbBCWoczF3zKC43DDg257nUjS6TmdL1yxKNR2ITRXPAvk2VjSKnhHMQxQ92UXi914NdkvU85ybx3VFfY+iEoXEe/F9biA+P0xkghJxaaVQ1XWD1rKKKwzjvu14rqWgWKs2WSd+12CdZFB0qzg2rQTR9AguAylANP1FgB7QTC1mkDF8w7GQ91M9VjLVoadQxT46l7a0jx7jI9Fn0HtZdL54KNMbnjiPnqjPKKhDha4H5vZpGJqH/in9SKnG9/p/rLJbVd3UX29yfKlvGv5DLf3RpcEBtcGO6vp+VBvsUBsHeB/vN5IGvORAjSyqw68HhKRzNA2DLK5vkM6jz2fRdwjROdpGB3TmIYLyH3qz/ZeFZToKqf3iUo9u3n8z/oJCX/oTPz+/naGhUakZWeWGvGKdo7BMP5lfqpvKK9K5SPnFuqn84top3pbqpgqLZekn84v1k/Q6v0xsC+k4v9ZP8pbelxokScdLdVMFJToX7SsqqXcVldW7ikrqnXkF2v70IyW6gIAwSsS7/fz8fhoeHv6in5/fC74X/mOWF4KCgl7yCwh4xc/Pj7TNz8/vVUmv+fn5bZdEr33lXU9+73vMV77ny1v6XqE9e17190/YRn8C3NR/9fyAQi56wS88/MWDBw++HBAQ8Iq/v/820p49e17dtWvXa7T1lnzcV39JHe999J303XQNfn4pfyO5hq7r/1WhC6JfjEQX+Cyiv0uSfPf/OcnfKYf8VtkqW2WrbJWtslX8/g/bgAU8p0CoCAAAAABJRU5ErkJggg==">
  <span class="tb-name">Capitalizer</span>
  <div class="wc">
    <div class="wc-btn" id="min" title="Minimize"><svg viewBox="0 0 10 10"><line x1="1" y1="5" x2="9" y2="5" stroke="currentColor" stroke-width="1.5"/></svg></div>
    <div class="wc-btn close" id="close" title="Close"><svg viewBox="0 0 10 10"><line x1="1.5" y1="1.5" x2="8.5" y2="8.5" stroke="currentColor" stroke-width="1.5"/><line x1="8.5" y1="1.5" x2="1.5" y2="8.5" stroke="currentColor" stroke-width="1.5"/></svg></div>
  </div>
</div>
<div class="panel">
  <div class="rows">
    <div class="row"><div class="label">UPPERCASE</div><div class="hk" id="hkU" tabindex="0">Unset</div></div>
    <div class="row"><div class="label">lowercase</div><div class="hk" id="hkL" tabindex="0">Unset</div></div>
    <div class="row" style="border-bottom:none"><div class="label">Start with Windows</div>
      <div class="toggle" id="tg" tabindex="0"><div class="knob"></div></div></div>
  </div>
</div>
<div class="statusbar">
  <div class="hint">Click a field, then press your shortcut</div>
</div>
<script>
var vp = window.chrome.webview;
var st = { hkU:{vk:0,mods:0}, hkL:{vk:0,mods:0}, autostart:false };
var capturing = null;
function keyName(vk){
  var m={8:'Backspace',9:'Tab',13:'Enter',20:'Caps Lock',27:'Esc',32:'Space',
    33:'Page Up',34:'Page Down',35:'End',36:'Home',37:'Left',38:'Up',39:'Right',40:'Down',
    45:'Insert',46:'Delete',
    112:'F1',113:'F2',114:'F3',115:'F4',116:'F5',117:'F6',118:'F7',119:'F8',120:'F9',121:'F10',122:'F11',123:'F12',
    186:';',187:'=',188:',',189:'-',190:'.',191:'/',192:'`',219:'[',220:'\\',221:']',222:"'"};
  if(m[vk]) return m[vk];
  if(vk>=65&&vk<=90) return String.fromCharCode(vk);
  if(vk>=48&&vk<=57) return String.fromCharCode(vk);
  if(vk>=96&&vk<=105) return 'Num '+(vk-96);
  return 'Key '+vk;
}
function combo(o){
  if(!o.vk) return 'Unset';
  var s='';
  if(o.mods&2) s+='Ctrl + ';
  if(o.mods&1) s+='Alt + ';
  if(o.mods&4) s+='Shift + ';
  return s+keyName(o.vk);
}
function render(){
  var u=document.getElementById('hkU'), l=document.getElementById('hkL'), t=document.getElementById('tg');
  u.textContent = capturing==='hkU' ? 'Press a key...' : combo(st.hkU);
  l.textContent = capturing==='hkL' ? 'Press a key...' : combo(st.hkL);
  u.classList.toggle('capturing', capturing==='hkU');
  l.classList.toggle('capturing', capturing==='hkL');
  t.classList.toggle('on', st.autostart);
}
function startCap(w){ capturing=w; render(); document.getElementById(w).focus(); }
function stopCap(){ capturing=null; render(); }
document.getElementById('hkU').onclick=function(){ startCap('hkU'); };
document.getElementById('hkL').onclick=function(){ startCap('hkL'); };
document.getElementById('tg').onclick=function(){ st.autostart=!st.autostart; render(); autoSave(); };
document.getElementById('min').onclick=function(){ vp.postMessage('minimize'); };
document.getElementById('close').onclick=function(){ vp.postMessage('cancel'); };
document.getElementById('titlebar').addEventListener('mousedown', function(e){
  if(e.button!==0 || e.target.closest('.wc-btn')) return;
  vp.postMessage('drag');
});
function autoSave(){ vp.postMessage('apply|'+st.hkU.vk+'|'+st.hkU.mods+'|'+st.hkL.vk+'|'+st.hkL.mods+'|'+(st.autostart?1:0)); }
document.addEventListener('keydown', function(e){
  if(capturing){
    e.preventDefault();
    var vk=e.keyCode;
    if(vk===16||vk===17||vk===18||vk===91||vk===92) return;
    if(vk===27){ stopCap(); return; }
    st[capturing]={vk:vk, mods:(e.ctrlKey?2:0)|(e.altKey?1:0)|(e.shiftKey?4:0)};
    stopCap();
    autoSave();
    return;
  }
  if(e.key==='Enter'||e.key==='Escape') vp.postMessage('cancel');
});
vp.addEventListener('message', function(e){
  var p=(''+e.data).split('|');
  if(p[0]==='init'){
    st.hkU={vk:+p[1],mods:+p[2]};
    st.hkL={vk:+p[3],mods:+p[4]};
    st.autostart=(p[5]==='1');
    render();
  }
});
render();
vp.postMessage('ready');
</script>
</body></html>)HTML";

}
