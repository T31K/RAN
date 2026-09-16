
-- viewShopPurchase

CREATE VIEW [dbo].[viewShopPurchase]
AS
SELECT   dbo.ShopPurchase.PurKey, dbo.ShopPurchase.UserUID, 
         dbo.ShopPurchase.ProductNum, dbo.ShopPurchase.PurPrice, 
         dbo.ShopPurchase.PurFlag, dbo.ShopPurchase.PurDate, 
         dbo.ShopPurchase.PurChgDate, dbo.ShopItemMap.ItemMain, 
         dbo.ShopItemMap.ItemSub, dbo.ShopItemMap.ItemName, 
         dbo.ShopPurFlag.PurFlagName
FROM     dbo.ShopPurchase INNER JOIN
         dbo.ShopItemMap ON 
         dbo.ShopPurchase.ProductNum = dbo.ShopItemMap.ProductNum LEFT OUTER JOIN
         dbo.ShopPurFlag ON dbo.ShopPurchase.PurFlag = dbo.ShopPurFlag.PurFlag


