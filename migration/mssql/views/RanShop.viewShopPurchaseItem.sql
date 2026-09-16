
-- view_ShopPurchaseItem.sql

CREATE VIEW [dbo].[viewShopPurchaseItem]
AS
SELECT TOP 100 PERCENT SUM(A.PurPrice) AS tPrice, A.ItemMain, A.ItemSub, B.ItemName
FROM   dbo.viewShopPurchase A INNER JOIN
       dbo.ShopItemMap B ON A.ProductNum = B.ProductNum
GROUP BY A.ItemMain, A.ItemSub, B.ItemName
ORDER BY A.ItemMain, A.ItemSub


